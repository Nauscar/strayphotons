#include "console/Console.hh"
#include "core/Logging.hh"
#include "core/RegisteredThread.hh"
#include "ecs/Ecs.hh"

#include <fstream>
#include <sstream>

namespace sp {
    CFunc<void> CFuncList("list", "Lists all CVar names, values, and descriptions", []() {
        for (auto &kv : GetConsoleManager().CVars()) {
            auto cvar = kv.second;
            if (cvar->IsValueType()) {
                logging::ConsoleWrite(logging::Level::Log, " > %s = %s", cvar->GetName(), cvar->StringValue());
            } else {
                logging::ConsoleWrite(logging::Level::Log, " > %s (func)", cvar->GetName());
            }

            auto desc = cvar->GetDescription();
            if (desc.size() > 0) {
                logging::ConsoleWrite(logging::Level::Log, " >   %s", desc);
            }
        }
    });

    CFunc<uint64, string> CFuncWait("wait", "Queue command for later (wait <ms> <command>)", [](uint64 dt, string cmd) {
        GetConsoleManager().QueueParseAndExecute(cmd, chrono_clock::now() + std::chrono::milliseconds(dt));
    });

    CFunc<string, string> CFuncToggle("toggle",
        "Toggle a CVar between values (toggle <cvar_name> [<value_a> <value_b>])",
        [](string cvarName, string args) {
            auto cvars = GetConsoleManager().CVars();
            auto cvarit = cvars.find(to_lower_copy(cvarName));
            if (cvarit != cvars.end()) {
                auto cvar = cvarit->second;
                if (cvar->IsValueType()) {
                    std::stringstream stream(args);
                    vector<string> values;
                    string value;
                    while (stream >> value) {
                        values.push_back(value);
                    }
                    cvar->ToggleValue(values.data(), values.size());
                } else {
                    logging::ConsoleWrite(logging::Level::Log, " > '%s' is not a cvar", cvarName);
                }
            } else {
                logging::ConsoleWrite(logging::Level::Log, " > '%s' undefined", cvarName);
            }
        });

    CFunc<void> CFuncPrintFocus("printfocus", "Print the current focus lock state", []() {
        auto lock = ecs::World.StartTransaction<ecs::Read<ecs::FocusLock>>();

        if (lock.Has<ecs::FocusLock>()) {
            auto &focusLock = lock.Get<ecs::FocusLock>();
            std::stringstream ss;
            ss << "Active focus layers: " << focusLock;
            Logf(ss.str());
        } else {
            Errorf("World does not have a FocusLock");
        }
    });

    CFunc<ecs::FocusLayer> CFuncAcquireFocus("acquirefocus",
        "Acquire focus for the specified layer",
        [](ecs::FocusLayer layer) {
            if (layer == ecs::FocusLayer::NEVER || layer == ecs::FocusLayer::ALWAYS) {
                auto lock = ecs::World.StartTransaction<ecs::Write<ecs::FocusLock>>();

                if (lock.Has<ecs::FocusLock>()) {
                    if (!lock.Get<ecs::FocusLock>().AcquireFocus(layer)) {
                        std::stringstream ss;
                        ss << layer;
                        Logf("Failed to acquire focus layer: %s", ss.str());
                    }
                } else {
                    Errorf("World does not have a FocusLock");
                }
            }
        });

    CFunc<ecs::FocusLayer> CFuncReleaseFocus("releasefocus",
        "Release focus for the specified layer",
        [](ecs::FocusLayer layer) {
            if (layer != ecs::FocusLayer::NEVER && layer != ecs::FocusLayer::ALWAYS) {
                auto lock = ecs::World.StartTransaction<ecs::Write<ecs::FocusLock>>();

                if (lock.Has<ecs::FocusLock>()) {
                    lock.Get<ecs::FocusLock>().ReleaseFocus(layer);
                } else {
                    Errorf("World does not have a FocusLock");
                }
            }
        });

    template<typename LockWrite, typename Callback>
    void mutateEntity(const string &entityStr, Callback callback) {
        ecs::Name entityName(entityStr, ecs::Name());
        if (!entityName) {
            Logf("Could not parse entity %s", entityStr);
            return;
        }
        auto lock = ecs::World.StartTransaction<ecs::Read<ecs::Name>, LockWrite>();
        auto entity = ecs::EntityWith<ecs::Name>(lock, entityName);
        if (!entity) {
            Logf("Entity %s not found", entityName.String());
            return;
        }
        callback(lock, entity);
    }

    template<typename Callback>
    void mutateEntityTransform(const string &entityStr, Callback callback) {
        mutateEntity<ecs::Write<ecs::TransformTree>>(entityStr, [&](auto lock, ecs::Entity entity) {
            if (!entity.Has<ecs::TransformTree>(lock)) {
                Logf("Entity %s has no transform", entityStr);
                return;
            }
            callback(lock, entity.Get<ecs::TransformTree>(lock));
        });
    }

    CFunc<string, glm::vec3> CFuncTranslate("translate",
        "Moves an entity a relative amount (translate <entity> <x> <y> <z>)",
        [](string entityStr, glm::vec3 value) {
            mutateEntityTransform(entityStr, [&](auto lock, ecs::TransformTree &transform) {
                transform.pose.Translate(value);
            });
        });

    CFunc<string, float, glm::vec3> CFuncRotate("rotate",
        "Rotates an entity a relative amount (rotate <entity> <degrees> <x> <y> <z>)",
        [](string entityStr, float degrees, glm::vec3 plane) {
            mutateEntityTransform(entityStr, [&](auto lock, ecs::TransformTree &transform) {
                transform.pose.Rotate(glm::radians(degrees), plane);
            });
        });

    CFunc<string, glm::vec3> CFuncScale("scale",
        "Scales an entity a relative amount (scale <entity> <x> <y> <z>)",
        [](string entityStr, glm::vec3 value) {
            mutateEntityTransform(entityStr, [&](auto lock, ecs::TransformTree &transform) {
                transform.pose.Scale(value);
            });
        });

    CFunc<string, double> CFuncSetSignal("setsignal",
        "Set a signal value (setsignal <entity>/<signal> <value>)",
        [](string signalStr, double value) {
            auto [entityName, signalName] = ecs::ParseSignalString(signalStr);

            auto lock = ecs::World.StartTransaction<ecs::Read<ecs::Name>, ecs::Write<ecs::SignalOutput>>();
            auto entity = ecs::EntityWith<ecs::Name>(lock, entityName);
            if (!entity) {
                Logf("Signal entity %s not found", entityName.String());
                return;
            }
            if (!entity.Has<ecs::SignalOutput>(lock)) {
                Logf("%s is not a signal output", entityName.String());
                return;
            }

            auto &signalComp = entity.Get<ecs::SignalOutput>(lock);
            signalComp.SetSignal(signalName, value);
        });

    CFunc<string, string> CFuncToggleSignal("togglesignal",
        "Toggle a signal between values (togglesignal <entity>/<signal> [<value_a> <value_b>])",
        [](string signalStr, string args) {
            auto [entityName, signalName] = ecs::ParseSignalString(signalStr);

            auto lock = ecs::World.StartTransaction<ecs::Read<ecs::Name>, ecs::Write<ecs::SignalOutput>>();
            auto entity = ecs::EntityWith<ecs::Name>(lock, entityName);
            if (!entity) {
                Logf("Signal entity %s not found", entityName.String());
                return;
            }
            if (!entity.Has<ecs::SignalOutput>(lock)) {
                Logf("%s is not a signal output", entityName.String());
                return;
            }

            std::stringstream stream(args);
            vector<string> values;
            string value;
            while (stream >> value) {
                values.push_back(value);
            }

            auto &signalComp = entity.Get<ecs::SignalOutput>(lock);
            auto signal = signalComp.GetSignal(signalName);
            ToggleBetweenValues(signal, values.data(), values.size());
            signalComp.SetSignal(signalName, signal);
        });

    CFunc<string> CFuncClearSignal("clearsignal",
        "Clear a signal value (clearsignal <entity>/<signal>)",
        [](string signalStr) {
            auto [entityName, signalName] = ecs::ParseSignalString(signalStr);

            auto lock = ecs::World.StartTransaction<ecs::Read<ecs::Name>, ecs::Write<ecs::SignalOutput>>();
            auto entity = ecs::EntityWith<ecs::Name>(lock, entityName);
            if (entity && entity.Has<ecs::SignalOutput>(lock)) {
                auto &signalComp = entity.Get<ecs::SignalOutput>(lock);
                signalComp.ClearSignal(signalName);
            }
        });

    CFunc<string, string> CFuncSendEvent("sendevent",
        "Send an entity an event (sendevent <entity>/<event> <value>)",
        [](string eventStr, string value) {
            auto [entityName, eventName] = ecs::ParseEventString(eventStr);

            ecs::Event event(eventName, ecs::Entity());
            if (!value.empty()) {
                if (is_float(value)) {
                    event.data = std::stof(value);
                } else {
                    event.data = value;
                }
            }

            auto lock = ecs::World.StartTransaction<ecs::SendEventsLock>();
            auto sent = ecs::EventBindings::SendEvent(lock, entityName, eventName, event);
            if (sent == 0) {
                Warnf("No event target found: %s%s", entityName.String(), eventName);
            } else {
                Logf("Sent %u events to %s%s", sent, entityName.String(), eventName);
            }
        });

    CFunc<size_t> CFuncTraceTecs("tracetecs", "Save an ECS performance trace (tracetecs <time_ms>)", [](size_t timeMs) {
        if (timeMs == 0) {
            Logf("Trace time must be specified in milliseconds.");
            return;
        }

        static std::atomic_bool tracing(false);
        bool current = tracing;
        if (current || !tracing.compare_exchange_strong(current, true)) {
            Logf("A performance trace is already in progress");
            return;
        }

        std::thread([timeMs] {
            tracy::SetThreadName("TecsTrace");
            ZoneScopedN("Tecs Trace");
            ecs::World.StartTrace();
            std::this_thread::sleep_for(std::chrono::milliseconds(timeMs));
            auto trace = ecs::World.StopTrace();

            for (auto &event : trace.transactionEvents) {
                std::stringstream ss;
                ss << event.thread;
                uint32_t thread_id;
                ss >> thread_id;
                trace.SetThreadName(tracy::GetThreadName(thread_id), event.thread);
            }

            std::ofstream traceFile("tecs-trace.csv");
            trace.SaveToCSV(traceFile);
            traceFile.close();

            Logf("Tecs performance trace complete");
            tracing = false;
        }).detach();
    });
} // namespace sp

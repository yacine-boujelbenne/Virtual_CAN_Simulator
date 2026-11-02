#include <gtest/gtest.h>
#include "runtime/Controller.hpp"
#include "runtime/Worker.hpp"
#include <cmath>
#include <csignal>
#include <cerrno>
#include <set>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
vclab::RuntimeConfig config() {
    vclab::RuntimeConfig result;
    result.worker_binary = VCLAB_WORKER_PATH;
    return result;
}
Json signature(const Json &state) {
    Json result;
    for (const auto *field : {"world", "frames", "timing", "performance", "project_hash"}) result[field] = state.at(field);
    return result;
}
Json findEcu(const Json &state, const std::string &name) {
    for (const auto &ecu : state.at("ecus")) if (ecu.at("name") == name) return ecu;
    throw std::runtime_error("Missing ECU: " + name);
}
}
TEST(NativeRuntime, IsolatedProcessesPreserveP0FramesEnvelopesAndPhysics) {
    vclab::Controller native(config());
    SimulationManager reference(config().scenario);
    Json controls = {{"action", "controls"}, {"values", {{"throttle", 0.6}}}};
    native.request(controls); reference.command(controls);
    auto actual = native.request({{"action", "step"}, {"count", 60}});
    auto expected = reference.command({{"action", "step"}, {"count", 60}});
    EXPECT_EQ(signature(actual), signature(expected));
    EXPECT_EQ(actual.at("cluster").at("worker_transport"), "native-process");
    std::set<int> pids;
    for (const auto &ecu : actual.at("ecus")) {
        int pid = ecu.at("container").at("kernel_pid");
        EXPECT_NE(pid, ::getpid()); pids.insert(pid);
    }
    EXPECT_EQ(pids.size(), 7u);
}
TEST(NativeProgramming, ValidatedDeploymentChangesRealEpsAndSurvivesProjectRestart) {
    auto settings = config(); settings.scenario = "resources/scenarios/ev_lateral.json";
    vclab::Controller native(settings);
    auto initial = native.request({{"action", "snapshot"}});
    auto definition = findEcu(initial, "EPS ECU").at("definition");
    // A real program edit: constant 0.2-rad EPS output, still using the ECU binding.
    definition["tasks"][1]["assign"]["steering_angle"] = 0.2;
    auto preview = native.request({{"action", "validate_ecu_update"}, {"name", "EPS ECU"}, {"definition", definition}});
    EXPECT_TRUE(preview.at("valid").get<bool>());
    EXPECT_EQ(native.request({{"action", "snapshot"}}), initial);
    auto deployed = native.request({{"action", "update_ecu"}, {"name", "EPS ECU"}, {"definition", definition}});
    EXPECT_EQ(deployed.at("project_hash"), preview.at("project_hash"));
    EXPECT_NE(deployed.at("project_hash"), initial.at("project_hash"));
    EXPECT_NE(findEcu(deployed,"EPS ECU").at("container").at("kernel_pid"),findEcu(initial,"EPS ECU").at("container").at("kernel_pid"));
    native.request({{"action", "controls"}, {"values", {{"gear",2}, {"throttle",0.5}}}});
    auto driven = native.request({{"action", "step"}, {"count",100}});
    EXPECT_NEAR(driven.at("world").at("steering_angle").get<double>(),0.2,1e-9);
    EXPECT_GT(driven.at("world").at("y").get<double>(),0.01);
    auto restarted = native.request({{"action", "restart_project"}});
    EXPECT_EQ(restarted.at("tick"),0);
    EXPECT_EQ(restarted.at("project_hash"),deployed.at("project_hash"));
    EXPECT_EQ(findEcu(restarted,"EPS ECU").at("definition"),definition);
    EXPECT_NEAR(native.request({{"action","step"}}).at("world").at("steering_angle").get<double>(),0.2,1e-9);
}
TEST(NativeProgramming, InvalidDeploymentPreservesStateAndRunningDeploymentIsRejected) {
    vclab::Controller native(config());
    auto before = native.request({{"action", "step"}, {"count",8}});
    auto definition = findEcu(before,"Motor ECU").at("definition");
    auto invalid = definition; invalid["tasks"][0]["assign"]["motor_rpm"] = "missing_input";
    EXPECT_THROW(native.request({{"action","update_ecu"},{"name","Motor ECU"},{"definition",invalid}}),std::exception);
    EXPECT_EQ(native.request({{"action","snapshot"}}),before);
    invalid=definition;invalid["messages"][0]["id"]=0x120; // Dynamics broadcast collision.
    EXPECT_THROW(native.request({{"action","update_ecu"},{"name","Motor ECU"},{"definition",invalid}}),std::exception);
    EXPECT_EQ(native.request({{"action","snapshot"}}),before);
    native.request({{"action","run"}});
    EXPECT_THROW(native.request({{"action","update_ecu"},{"name","Motor ECU"},{"definition",definition}}),std::exception);
    EXPECT_TRUE(native.request({{"action","snapshot"}}).at("running").get<bool>());
}
TEST(NativeRuntime, FailedPartialTickRetriesFrozenInputsWithoutAdvancingTwice) {
    vclab::Controller native(config()), reference(config());
    Json controls = {{"action", "controls"}, {"values", {{"throttle", 0.6}}}};
    native.request(controls); reference.request(controls);
    native.request({{"action", "step"}, {"count", 8}});
    reference.request({{"action", "step"}, {"count", 8}});
    auto before = native.request({{"action", "snapshot"}});
    native.request({{"action", "node_fault"}, {"name", "BMS ECU"}, {"mode", "halt"}});
    EXPECT_THROW(native.request({{"action", "step"}}), std::exception);
    auto failed = native.request({{"action", "snapshot"}});
    EXPECT_EQ(failed.at("time"), before.at("time"));
    EXPECT_EQ(failed.at("world"), before.at("world"));
    EXPECT_FALSE(failed.at("cluster").at("last_error").is_null());
    EXPECT_THROW(native.request(controls), std::exception);
    native.request({{"action", "node_fault"}, {"name", "BMS ECU"}, {"mode", "restore"}});
    EXPECT_EQ(signature(native.request({{"action", "step"}})), signature(reference.request({{"action", "step"}})));
}
TEST(NativeRepair, RunResumesRestoredWorkerAndCommitsPendingTickOnce) {
    vclab::Controller native(config()),reference(config());
    native.request({{"action","node_fault"},{"name","Radar ECU"},{"mode","halt"}});
    EXPECT_THROW(native.request({{"action","run"}}),std::exception);
    EXPECT_THROW(native.request({{"action","step"}}),std::exception);
    native.request({{"action","node_fault"},{"name","Radar ECU"},{"mode","restore"}});
    auto resumed=native.request({{"action","run"}});
    EXPECT_EQ(resumed.at("tick"),1);EXPECT_TRUE(resumed.at("running").get<bool>());
    EXPECT_EQ(signature(resumed),signature(reference.request({{"action","step"}})));
    EXPECT_EQ(native.request({{"action","snapshot"}}).at("tick"),1);
}
TEST(NativeRepair, TopologyDraftChangesWorkersAndMetadataSurvivesRestartAndExport) {
    vclab::Controller native(config());auto original=native.request({{"action","snapshot"}});
    native.request({{"action","set_ui_layout"},{"positions",{{"Motor ECU",{45,-20}}}}});
    auto project=native.request({{"action","export_project"}});
    EXPECT_EQ(project.at("project_hash"),original.at("project_hash"));
    EXPECT_EQ(project.at("presentation").at("node_offsets").at("Motor ECU"),Json({45,-20}));
    auto invalid=project;invalid["scenario"]["buses"][0]["id"]="missing";
    auto before=native.request({{"action","snapshot"}});
    EXPECT_THROW(native.request({{"action","apply_project_draft"},{"project",invalid}}),std::exception);
    EXPECT_EQ(native.request({{"action","snapshot"}}),before);
    Json definition;for(const auto&d:project.at("ecu_definitions"))if(d.at("name")=="Dynamics ECU")definition=d;
    definition["name"]="Added ECU";definition["bindings"]=Json::object();
    definition["messages"]=Json::array();
    project["ecu_definitions"]["desktop/new.json"]=definition;
    project["scenario"]["ecus"].push_back("desktop/new.json");
    auto preview=native.request({{"action","validate_project_draft"},{"project",project}});
    EXPECT_TRUE(preview.at("valid").get<bool>());EXPECT_EQ(native.request({{"action","snapshot"}}),before);
    auto applied=native.request({{"action","apply_project_draft"},{"project",project}});
    EXPECT_EQ(applied.at("ecus").size(),8u);EXPECT_EQ(applied.at("cluster").at("connected"),8);
    auto restarted=native.request({{"action","restart_project"}});
    EXPECT_EQ(restarted.at("presentation"),before.at("presentation"));
    EXPECT_EQ(restarted.at("project_hash"),preview.at("project_hash"));
}
TEST(NativeRuntime, DeadWorkerNeverFallsBackAndResetReplacesProcesses) {
    vclab::Controller native(config());
    auto before = native.request({{"action", "snapshot"}});
    int pid = before.at("ecus")[0].at("container").at("kernel_pid");
    ASSERT_EQ(::kill(pid, SIGKILL), 0);
    EXPECT_THROW(native.request({{"action", "step"}}), std::exception);
    EXPECT_EQ(native.request({{"action", "snapshot"}}).at("time"), 0);
    auto reset = native.request({{"action", "reset"}});
    EXPECT_GT(reset.at("epoch").get<uint64_t>(), before.at("epoch").get<uint64_t>());
    EXPECT_NE(reset.at("ecus")[0].at("container").at("kernel_pid"), pid);
    EXPECT_EQ(native.request({{"action", "step"}}).at("tick"), 1);
}
TEST(NativeRuntime, ProjectRoundTripIsStrictTransactionalAndKeepsEpochMonotonic) {
    vclab::Controller native(config());
    auto project = native.request({{"action", "export_project"}});
    native.request({{"action", "reset"}}); native.request({{"action", "reset"}});
    auto before = native.request({{"action", "step"}, {"count", 12}});
    auto corrupt = project; corrupt["project_hash"] = "sha256:wrong";
    EXPECT_THROW(native.request({{"action", "import_project"}, {"project", corrupt}}), std::exception);
    EXPECT_EQ(native.request({{"action", "snapshot"}}), before);
    native.request({{"action", "import_project"}, {"project", project}});
    auto imported = native.request({{"action", "snapshot"}});
    EXPECT_GT(imported.at("epoch").get<uint64_t>(), before.at("epoch").get<uint64_t>());
    EXPECT_EQ(imported.at("tick"), 0);
    EXPECT_EQ(native.request({{"action", "export_project"}}).at("project_hash"), project.at("project_hash"));
    EXPECT_EQ(native.request({{"action", "step"}}).at("tick"), 1);
}
TEST(NativeRuntime, RemoteDiagnosticsUseRealIsoTpOnBothLinksWithoutAdvancingClock) {
    vclab::Controller native(config());
    for (bool fd : {false, true}) {
        auto result = native.request({{"action", "diagnostic"}, {"ecu", "Motor ECU"}, {"bytes", {34, 241, 144}}, {"fd", fd}});
        EXPECT_EQ(result.at("bytes").size(), 20u);
        EXPECT_EQ(result.at("bytes")[0], 0x62);
        if (fd) EXPECT_EQ(result.at("frame_count"), 2); else EXPECT_GT(result.at("frame_count").get<unsigned>(), 2u);
        for (const auto &frame : result.at("frames")) EXPECT_EQ(frame.at("fd"), fd);
    }
    auto negative = native.request({{"action", "diagnostic"}, {"ecu", "Motor ECU"}, {"bytes", {39, 1}}});
    EXPECT_EQ(negative.at("bytes"), Json({127, 39, 17}));
    EXPECT_EQ(native.request({{"action", "snapshot"}}).at("time"), 0);
    EXPECT_THROW(native.request({{"action", "diagnostic"}, {"ecu", "Motor ECU"}, {"bytes", {-1}}}), std::exception);
}
TEST(NativeRuntime, ReserveAssignmentIsIsolatedAndReleasedOnReset) {
    vclab::Controller native(config());
    auto state = native.request({{"action", "step"}, {"count", 5}});
    auto definition = state.at("ecus")[0].at("definition");
    definition["name"] = "Observation ECU"; definition["bindings"] = Json::object();
    for (auto &message : definition["messages"]) if (message.at("direction") == "tx") message["id"] = message.at("id").get<unsigned>() + 1000;
    auto added = native.request({{"action", "add_ecu"}, {"definition", definition}});
    EXPECT_EQ(added.at("ecus").size(), 8u);
    EXPECT_EQ(added.at("cluster").at("reserve_slots"), 0);
    EXPECT_EQ(native.request({{"action", "step"}}).at("tick"), 6);
    EXPECT_THROW(native.request({{"action", "add_ecu"}, {"definition", definition}}), std::exception);
    auto reset = native.request({{"action", "reset"}});
    EXPECT_EQ(reset.at("ecus").size(), 7u);
    EXPECT_EQ(reset.at("cluster").at("reserve_slots"), 1);
}
TEST(NativeRuntime, MissingMappingsAndInvalidCommandsFailExplicitly) {
    auto remote = config(); remote.endpoints = {{"Motor ECU", "http://127.0.0.1:1"}};
    EXPECT_THROW(vclab::Controller missing(remote), std::exception);
    remote.endpoints["ABS ECU"] = remote.endpoints["Motor ECU"];
    EXPECT_THROW(vclab::Controller duplicate(remote), std::exception);
    vclab::Controller native(config());
    for (Json count : {Json(-1), Json(0), Json(1.5), Json(1001), Json(true)})
        EXPECT_THROW(native.request({{"action", "step"}, {"count", count}}), std::exception);
    EXPECT_THROW(native.request({{"action", "prepare_tick"}}), std::exception);
}
TEST(NativeRuntime, WorkerConfigurationFailurePreservesPreviousKernel) {
    vclab::Worker worker;
    EXPECT_THROW(worker.request({{"action", "snapshot"}}), std::exception);
    SimulationManager core(config().scenario);
    auto definition = core.snapshot().at("ecus")[0].at("definition");
    auto original = worker.request({{"action", "configure"}, {"definition", definition}, {"epoch", 12}});
    definition.erase("state_machine");
    EXPECT_THROW(worker.request({{"action", "configure"}, {"definition", definition}}), std::exception);
    EXPECT_EQ(worker.request({{"action", "snapshot"}}), original);
}
TEST(NativeRuntime, OwnedWorkersAreReapedWhenControllerCloses) {
    std::vector<int> pids;
    {
        vclab::Controller native(config());
        auto state = native.request({{"action", "snapshot"}});
        for (const auto &ecu : state.at("ecus"))
            pids.push_back(ecu.at("container").at("kernel_pid"));
    }
    for (int pid : pids) { EXPECT_EQ(::kill(pid, 0), -1); EXPECT_EQ(errno, ESRCH); }
}
TEST(NativePlant, LateralProfileIsOptIn) {
    SimulationManager basic("resources/scenarios/ev_basic.json");
    auto state = basic.snapshot();
    EXPECT_FALSE(state.at("world").contains("heading"));
    EXPECT_FALSE(state.at("controls").contains("steering"));
    EXPECT_THROW(basic.command({{"action", "controls"}, {"values", {{"steering", 0.4}}}}), std::exception);
}
TEST(NativePlant, SteeringChangesHeadingAndLateralPositionThroughEps) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    sim.command({{"action", "controls"}, {"values", {{"gear", 2}, {"throttle", 0.5}, {"steering", 0.5}}}});
    sim.step(200);
    auto state = sim.snapshot();
    auto eps = findEcu(state, "EPS ECU");
    EXPECT_NEAR(eps.at("values").at("steering_angle").get<double>(), 0.3, 1e-9);
    EXPECT_NEAR(state.at("world").at("steering_angle").get<double>(), 0.3, 1e-9);
    EXPECT_GT(state.at("world").at("speed").get<double>(), 1.0);
    EXPECT_GT(state.at("world").at("heading").get<double>(), 0.1);
    EXPECT_GT(state.at("world").at("y").get<double>(), 0.1);
    EXPECT_GT(state.at("world").at("x").get<double>(), 1.0);
}
TEST(NativePlant, PndSelectsParkNeutralAndDriveWithReverseRejected) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    auto controls = [&](Json values) { return sim.command({{"action", "controls"}, {"values", values}}); };
    controls({{"gear", 2}, {"throttle", 0.5}});
    sim.step(100);
    double driveSpeed = sim.snapshot().at("world").at("speed").get<double>();
    ASSERT_GT(driveSpeed, 1.0);

    controls({{"gear", 1}});
    sim.step(100);
    double neutralSpeed = sim.snapshot().at("world").at("speed").get<double>();
    EXPECT_LT(neutralSpeed, driveSpeed);

    controls({{"gear", 2}});
    sim.step(50);
    EXPECT_GT(sim.snapshot().at("world").at("speed").get<double>(), neutralSpeed);
    EXPECT_THROW(controls({{"gear", 0}}), std::exception);
    EXPECT_THROW(controls({{"gear", -1}}), std::exception);
    EXPECT_THROW(controls({{"gear", 3}}), std::exception);
    EXPECT_EQ(sim.snapshot().at("controls").at("gear"), 2);

    controls({{"throttle", 0}, {"brake", 1}});
    for (int i = 0; i < 300 && sim.snapshot().at("world").at("speed").get<double>() > 0; ++i) sim.step();
    ASSERT_EQ(sim.snapshot().at("world").at("speed"), 0);
    controls({{"brake", 0}, {"gear", 0}});
    auto parked = sim.snapshot().at("world");
    controls({{"throttle", 1}});
    sim.step(50);
    auto held = sim.snapshot().at("world");
    EXPECT_EQ(held.at("speed"), 0);
    EXPECT_EQ(held.at("x"), parked.at("x"));
    EXPECT_EQ(held.at("y"), parked.at("y"));

    controls({{"throttle", 0.5}, {"gear", 1}});
    sim.step(50);
    EXPECT_EQ(sim.snapshot().at("world").at("speed"), 0);
    controls({{"gear", 2}});
    sim.step(50);
    EXPECT_GT(sim.snapshot().at("world").at("speed").get<double>(), 0);
}
TEST(NativePlant, BrakingOwnershipAndActuatorExclusivityRemain) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    sim.command({{"action", "controls"}, {"values", {{"gear", 2}, {"throttle", 0.6}}}});
    sim.step(100);
    double moving = sim.snapshot().at("world").at("speed").get<double>();
    sim.command({{"action", "controls"}, {"values", {{"throttle", 0}, {"brake", 1}}}});
    sim.step(20);
    auto braked = sim.snapshot();
    EXPECT_LT(braked.at("world").at("speed").get<double>(), moving);
    auto abs = findEcu(braked, "ABS ECU");
    EXPECT_GT(abs.at("values").at("pressure").get<double>(), 0);
    EXPECT_NEAR(braked.at("world").at("brake_pressure").get<double>(), abs.at("values").at("pressure").get<double>(), 1e-12);

    auto conflicting = [](const std::string &name, const std::string &actuator) {
        return Json{{"schema_version", 1}, {"name", name}, {"inputs", {"ignition"}}, {"variables", {{"value", 0}}},
            {"tasks", Json::array()}, {"bindings", {{actuator, "value"}}}, {"messages", Json::array()},
            {"state_machine", {{"initial", "OFF"}, {"states", {"OFF"}}, {"transitions", Json::array()}}}, {"dtcs", Json::array()}};
    };
    EXPECT_THROW(sim.command({{"action", "add_ecu"}, {"definition", conflicting("Second motor", "torque_request")}}), std::exception);
    EXPECT_THROW(sim.command({{"action", "add_ecu"}, {"definition", conflicting("Second EPS", "steering_angle")}}), std::exception);
    EXPECT_THROW(sim.command({{"action", "add_ecu"}, {"definition", conflicting("Second brake", "brake_pressure")}}), std::exception);
}

namespace {
Json projectOf(SimulationManager &sim) { return sim.command({{"action", "export_project"}}); }
void importVariant(SimulationManager &sim, Json project) {
    project["project_hash"] = sim.command({{"action", "hash_project"}, {"project", project}}).at("project_hash");
    sim.command({{"action", "import_project"}, {"project", project}});
}
void slowTransmission(Json &project) {
    for (auto &item : project.at("ecu_definitions").items()) {
        if (item.value().at("name").get<std::string>() != "Transmission ECU") continue;
        item.value()["hardware"] = Json{{"cpu_mhz", 1}, {"ram_kib", 128}, {"flash_kib", 512}, {"task_budget_us", 8000},
            {"adc", {{"resolution_bits", 12}, {"channels", Json::object()}}},
            {"can_controller", {{"fd_capable", true}, {"rx_fifo_frames", 128}}},
            {"gpio_outputs", Json::array()}, {"execution_profile", "resource-v1"},
            {"scheduler", {{"deadline_us", 100000}, {"watchdog_us", 100000}, {"queue_bytes", 65536}, {"max_pending_jobs", 16}}}};
    }
}
}
TEST(NativePlant, DelayedParkAppliesPrelockBrakingInsteadOfTeleporting) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    auto project = projectOf(sim);
    project["scenario"]["plant"]["initial_gear"] = 2;
    project["scenario"]["initial_state"]["speed"] = 0.4;
    slowTransmission(project);
    importVariant(sim, project);

    sim.command({{"action", "controls"}, {"values", {{"gear", 2}, {"throttle", 1}, {"steering", 0}, {"aeb_enabled", 0}}}});
    bool requested = false, sawPendingPark = false;
    std::vector<std::pair<double, double>> parkHistory;
    for (int i = 0; i < 200; ++i) {
        sim.step();
        auto world = sim.snapshot().at("world");
        double gear = world.at("gear_position").get<double>();
        double speed = world.at("speed").get<double>();
        if (!requested && gear == 2 && speed > 0.45 && speed <= 0.5 && world.at("drive_permit").get<double>() == 1) {
            sim.command({{"action", "controls"}, {"values", {{"gear", 0}}}});
            requested = true;
        }
        if (requested) {
            parkHistory.emplace_back(gear, speed);
            if (gear == 0 && speed > 0.5 + 1e-12) sawPendingPark = true;
        }
    }
    ASSERT_TRUE(requested) << "Park request was never admitted at or below the parking-lock speed";
    ASSERT_TRUE(sawPendingPark) << "Delayed Park application above the limit (the defect under test) was not reproduced";

    const double rampDrop = 2.0 * 0.01 + 1e-9;
    for (size_t i = 1; i < parkHistory.size(); ++i) {
        if (parkHistory[i - 1].first == 0 && parkHistory[i].first == 0) {
            EXPECT_FALSE(parkHistory[i - 1].second > 0.5 + 1e-12 && parkHistory[i].second == 0)
                << "Parking lock teleported speed to zero above the limit: " << parkHistory[i - 1].second << " -> " << parkHistory[i].second;
        }
    }
    auto parked = sim.snapshot().at("world");
    EXPECT_EQ(parked.at("gear_position").get<double>(), 0);
    EXPECT_EQ(parked.at("speed").get<double>(), 0);
    for (const auto &[gear, speed] : parkHistory) EXPECT_GE(speed, 0.0);
}
TEST(NativePlant, ImportRejectsMovingParkInitialState) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    std::string originalHash = sim.snapshot().at("project_hash");
    const char *message = nullptr;
    try {
        auto project = projectOf(sim);
        project["scenario"]["initial_state"]["speed"] = 10.0;
        importVariant(sim, project);
        FAIL() << "Moving Park initial state was accepted";
    } catch (const std::exception &e) { message = e.what(); }
    EXPECT_NE(message, nullptr);
    EXPECT_EQ(sim.snapshot().at("project_hash"), originalHash);

    auto parkedSlow = projectOf(sim);
    parkedSlow["scenario"]["initial_state"]["speed"] = 0.2;
    importVariant(sim, parkedSlow);
    auto world = sim.snapshot().at("world");
    EXPECT_EQ(world.at("gear_position").get<double>(), 0);
    EXPECT_NEAR(world.at("speed").get<double>(), 0.2, 1e-12);

    auto coastingNeutral = projectOf(sim);
    coastingNeutral["scenario"]["plant"]["initial_gear"] = 1;
    coastingNeutral["scenario"]["initial_state"]["speed"] = 10.0;
    importVariant(sim, coastingNeutral);
    world = sim.snapshot().at("world");
    EXPECT_EQ(world.at("gear_position").get<double>(), 1);
    EXPECT_NEAR(world.at("speed").get<double>(), 10.0, 1e-12);
}
TEST(NativePlant, WorldSteeringAngleMatchesSaturatedPlantAngle) {
    SimulationManager sim("resources/scenarios/ev_lateral.json");
    auto project = projectOf(sim);
    project["scenario"]["plant"]["max_steer_angle_rad"] = 0.1;
    importVariant(sim, project);

    sim.command({{"action", "controls"}, {"values", {{"gear", 2}, {"throttle", 0.5}, {"steering", 1}, {"aeb_enabled", 0}}}});
    sim.step(100);
    auto state = sim.snapshot();
    auto world = state.at("world");
    EXPECT_NEAR(world.at("steering_angle").get<double>(), 0.1, 1e-12);
    EXPECT_NEAR(findEcu(state, "EPS ECU").at("values").at("steering_angle").get<double>(), 0.6, 1e-9);
    double speed = world.at("speed").get<double>();
    ASSERT_GT(speed, 0);
    double steer = world.at("steering_angle").get<double>();
    EXPECT_NEAR(world.at("yaw_rate").get<double>(), speed / 2.7 * std::tan(steer), 1e-9);
}

namespace {
Json gripProject(vclab::Controller &native,double speed=10,double mass=1500){
    auto p=native.request({{"action","export_project"}});
    p["scenario"]["plant"]["model"]="bicycle_grip_v2";
    p["scenario"]["plant"]["mass_kg"]=mass;p["scenario"]["plant"]["initial_gear"]=1;
    p["scenario"]["initial_state"]["speed"]=speed;
    p["project_hash"]=native.request({{"action","hash_project"},{"project",p}}).at("project_hash");return p;
}
vclab::RuntimeConfig lateralConfig(){auto c=config();c.scenario="resources/scenarios/ev_lateral.json";return c;}
}
TEST(PhysicsV2, CoastForcesMatchNewtonWithSlopeAndHeadwind){
    vclab::Controller native(lateralConfig());native.request({{"action","import_project"},{"project",gripProject(native)}});
    native.request({{"action","controls"},{"values",{{"road_grade_deg",5},{"headwind_mps",20},{"ignition",0}}}});
    auto s=native.request({{"action","step"}});const auto&w=s.at("world");
    const double angle=5*3.141592653589793/180;
    const double aero=.5*1.225*.7*30*30,rolling=.012*1500*9.81*std::cos(angle),grade=1500*9.81*std::sin(angle);
    EXPECT_NEAR(w.at("acceleration").get<double>(),-(aero+rolling+grade)/1500,1e-10);
    EXPECT_NEAR(w.at("drag_force_n").get<double>(),aero,1e-10);
    EXPECT_NEAR(w.at("kinetic_energy_j").get<double>(),.5*1500*std::pow(w.at("speed").get<double>(),2),1e-8);
    EXPECT_EQ(w.at("traction_force_n"),0);
}
TEST(PhysicsV2, SteeringRateAndCombinedTireDemandStayBounded){
    vclab::Controller native(lateralConfig());native.request({{"action","import_project"},{"project",gripProject(native,20)}});
    native.request({{"action","controls"},{"values",{{"steering",1},{"road_friction",.2}}}});
    auto first=native.request({{"action","step"}});EXPECT_NEAR(first["world"]["steering_angle"].get<double>(),.008,1e-12);
    auto s=native.request({{"action","step"},{"count",80}});const auto&w=s.at("world");
    EXPECT_LE(std::abs(w.at("lateral_accel_mps2").get<double>()),.2*9.81+1e-9);
    EXPECT_GT(w.at("heading").get<double>(),0);EXPECT_LE(w.at("tire_utilization").get<double>(),1);
    native.request({{"action","controls"},{"values",{{"brake",1},{"steering",1}}}});
    s=native.request({{"action","step"},{"count",10}});
    const double longitudinal=(s["world"]["traction_force_n"].get<double>()-s["world"]["braking_force_n"].get<double>())/1500;
    EXPECT_LE(std::hypot(longitudinal,s["world"]["lateral_accel_mps2"].get<double>()),.2*9.81+1e-8);
}
TEST(PhysicsV2, BrakeEnergyAndStoppingDistanceAreFiniteAndMassAffectsCoast){
    double deceleration[2];int i=0;
    for(double mass:{1000.,2500.}){
        vclab::Controller native(lateralConfig());native.request({{"action","import_project"},{"project",gripProject(native,10,mass)}});
        deceleration[i++]=native.request({{"action","step"}})["world"]["acceleration"];
    }
    EXPECT_LT(deceleration[0],deceleration[1]);
    vclab::Controller native(lateralConfig());native.request({{"action","import_project"},{"project",gripProject(native,10)}});
    native.request({{"action","controls"},{"values",{{"brake",1},{"aeb_enabled",0}}}});
    auto s=native.request({{"action","step"},{"count",600}});auto w=s.at("world");
    EXPECT_EQ(w.at("speed"),0);EXPECT_GT(w.at("brake_energy_j").get<double>(),0);
    EXPECT_LE(w.at("brake_energy_j").get<double>(),75000+1e-6);
    EXPECT_GT(w.at("position").get<double>(),0);EXPECT_LT(w.at("position").get<double>(),50);
    EXPECT_THROW(native.request({{"action","controls"},{"values",{{"road_grade_deg",21},{"throttle",1}}}}),std::exception);
    EXPECT_EQ(native.request({{"action","snapshot"}})["controls"]["throttle"],0);
}

TEST(InteractiveTraffic, GeometrySelectionMotionAndCollisionAreDeterministic){
    vclab::TrafficWorld world;
    world.actors=vclab::TrafficWorld::validated(Json::array({{{"id","side"},{"x",10},{"y",4},{"speed",0}},{{"id","front"},{"x",34.5},{"y",0},{"speed",5}}}));
    auto o=world.observe(0,0,0,15);EXPECT_EQ(o.lead,"front");EXPECT_NEAR(o.gap,30,1e-10);EXPECT_NEAR(o.closing,10,1e-10);EXPECT_FALSE(o.collision);
    world.step(1);EXPECT_NEAR(world.actors[1]["x"].get<double>(),39.5,1e-10);
    EXPECT_TRUE(world.observe(39.5,0,0,0).collision);
    EXPECT_EQ(world.observe(0,0,3.141592653589793,15).lead,"");
    EXPECT_THROW(vclab::TrafficWorld::validated(Json::array({{{"id","bad"},{"speed",-1}}})),std::exception);
}
TEST(InteractiveTraffic, FollowingSlowsThroughCanAndMaintainsGap){
    auto c=config();c.scenario="resources/scenarios/ev_interactive.json";vclab::Controller native(c);
    auto p=native.request({{"action","export_project"}});p["scenario"]["plant"]["initial_gear"]=2;p["scenario"]["initial_state"]["speed"]=20;
    for(const auto*wheel:{"wheel_fl","wheel_fr","wheel_rl","wheel_rr"})p["scenario"]["initial_state"][wheel]=20;
    p["project_hash"]=native.request({{"action","hash_project"},{"project",p}})["project_hash"];native.request({{"action","import_project"},{"project",p}});
    native.request({{"action","traffic_actor"},{"operation","add"},{"id","lead"},{"actor",{{"x",84.5},{"y",0},{"speed",8},{"target_speed",8}}}});
    native.request({{"action","controls"},{"values",{{"assist_enabled",1},{"target_speed",80},{"aeb_enabled",1}}}});
    bool fractional=false,canBrake=false;double minimumGap=1e9;Json s;
    for(int i=0;i<30;++i){s=native.request({{"action","step"},{"count",100}});minimumGap=std::min(minimumGap,s["world"]["lead_distance"].get<double>());for(const auto&f:s.at("frames"))if(f.at("id")==1104){canBrake=true;const auto value=f.at("signals").at("aeb_request").get<double>();fractional|=value>0&&value<.99;}EXPECT_EQ(s["world"]["collision"],0);EXPECT_EQ(s["world"].value("collision_events",0.0),0);}
    EXPECT_TRUE(canBrake);EXPECT_TRUE(fractional);EXPECT_GT(minimumGap,5);EXPECT_NEAR(s["world"]["speed"].get<double>(),8,1.5);EXPECT_LT(s["world"]["speed"].get<double>(),12);
    auto saved=native.request({{"action","export_project"}});EXPECT_EQ(saved["scenario"]["traffic"].size(),1u);
    native.request({{"action","traffic_actor"},{"operation","remove"},{"id","lead"}});s=native.request({{"action","step"},{"count",200}});EXPECT_EQ(s["world"]["lead_distance"],600);EXPECT_GT(s["world"]["speed"].get<double>(),9);
}
TEST(InteractiveTraffic, StateStressIsAtomicAndContactHistoryIsLatched){
    auto c=config();c.scenario="resources/scenarios/ev_interactive.json";vclab::Controller native(c);
    auto before=native.request({{"action","snapshot"}});
    EXPECT_THROW(native.request({{"action","environment"},{"values",{{"motor_temperature",120},{"soc",101}}}}),std::exception);
    EXPECT_EQ(native.request({{"action","snapshot"}}),before);
    native.request({{"action","environment"},{"values",{{"motor_temperature",120},{"battery_temperature",55},{"soc",10}}}});
    auto s=native.request({{"action","step"},{"count",20}});EXPECT_EQ(findEcu(s,"Motor ECU").at("state"),"FAULT");
    native.request({{"action","traffic_actor"},{"operation","add"},{"id","contact"},{"actor",{{"x",3},{"y",0},{"speed",20}}}});
    s=native.request({{"action","snapshot"}});EXPECT_EQ(s["world"]["collision"],1);EXPECT_EQ(s["world"]["collision_events"],1);
    native.request({{"action","traffic_actor"},{"operation","remove"},{"id","contact"}});s=native.request({{"action","snapshot"}});EXPECT_EQ(s["world"]["collision"],0);EXPECT_EQ(s["world"]["collision_events"],1);
}
TEST(PhysicsV2, DriveRatioAndTorqueClampChangeMeasuredForce){
    double forces[2];int index=0;
    for(double ratio:{6.,12.}){vclab::Controller native(lateralConfig());auto p=gripProject(native,0);p["scenario"]["plant"]["initial_gear"]=2;p["scenario"]["plant"]["motor_torque_limit_nm"]=160;p["scenario"]["plant"]["final_drive_ratio"]=ratio;p["project_hash"]=native.request({{"action","hash_project"},{"project",p}}).at("project_hash");native.request({{"action","import_project"},{"project",p}});native.request({{"action","controls"},{"values",{{"throttle",1}}}});auto s=native.request({{"action","step"},{"count",20}});forces[index++]=s["world"]["traction_force_n"];}
    EXPECT_NEAR(forces[0],3000,1e-8);EXPECT_NEAR(forces[1],6000,1e-8);
}

#include <unity.h>

#include <cmath>
#include <cstring>

#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Six-position accelerometer calibration (#22): `cal <face>` six times, then
// `cal save`; every reading is then corrected as (raw - offset) / scale.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

// A sensor with these per-axis errors: raw = OFFSET + SCALE * true.
static const float OFFSET[3] = {0.02f, -0.03f, 0.05f};
static const float SCALE[3] = {1.01f, 0.98f, 0.88f};

static void scriptStill(FakeImu& imu, float ax, float ay, float az, int count) {
  for (int i = 0; i < count; ++i) imu.samples.push_back({ax, ay, az, 0, 0, 0});
}

// What the sensor above reads when true gravity is (x, y, z).
static void scriptTrue(FakeImu& imu, float x, float y, float z, int count) {
  scriptStill(imu, OFFSET[0] + SCALE[0] * x, OFFSET[1] + SCALE[1] * y, OFFSET[2] + SCALE[2] * z,
              count);
}

static const float FACE_GRAVITY[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
static const CalFace FACES[6] = {CalFace::PosX, CalFace::NegX, CalFace::PosY,
                                 CalFace::NegY, CalFace::PosZ, CalFace::NegZ};

static CalResult captureFace(FakeHardware& hw, RtcState& state, int i) {
  const float* g = FACE_GRAVITY[i];
  scriptTrue(hw.imu, g[0], g[1], g[2], 100);
  return runCalCommand(CalStep::Face, FACES[i], state, testConfig(), hw.ports());
}

static void captureAllFaces(FakeHardware& hw, RtcState& state) {
  for (int i = 0; i < 6; ++i) TEST_ASSERT_TRUE(captureFace(hw, state, i).ok);
}

static AccelCalibration sensorCalibration() {
  AccelCalibration c{};
  for (int k = 0; k < 3; ++k) {
    c.offset[k] = OFFSET[k];
    c.scale[k] = SCALE[k];
  }
  return c;
}

// Gravity for pitch 10 deg, roll -20 deg: (-sin10, cos10*sin(-20), cos10*cos20).
static const float TILTED[3] = {-0.173648f, -0.336824f, 0.925417f};

void setUp() {}
void tearDown() {}

void test_no_calibration_leaves_readings_unchanged() {
  FakeHardware hw;
  RtcState state;
  scriptStill(hw.imu, 0.0f, 0.0f, 0.876f, 200);

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.reading.accelCalibrated);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.876f, r.reading.accelG);
}

void test_six_faces_then_save_stores_the_offset_and_scale() {
  FakeHardware hw;
  RtcState state;
  captureAllFaces(hw, state);

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(hw.store.hasAccelCalibration);
  for (int k = 0; k < 3; ++k) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[k], hw.store.accelCalibration.offset[k]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, SCALE[k], hw.store.accelCalibration.scale[k]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[k], r.calibration.offset[k]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, SCALE[k], r.calibration.scale[k]);
  }
}

void test_faces_can_be_taken_in_any_order_and_retaken() {
  FakeHardware hw;
  RtcState state;
  scriptStill(hw.imu, 0.0f, 0.0f, 0.5f, 100);  // a bad +Z first: retaken below
  TEST_ASSERT_TRUE(runCalCommand(CalStep::Face, CalFace::PosZ, state, testConfig(), hw.ports()).ok);
  for (int i = 5; i >= 0; --i) TEST_ASSERT_TRUE(captureFace(hw, state, i).ok);

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SCALE[2], hw.store.accelCalibration.scale[2]);
}

void test_face_reply_reports_the_average_and_the_faces_so_far() {
  FakeHardware hw;
  RtcState state;
  captureFace(hw, state, 0);

  CalResult r = captureFace(hw, state, 4);  // +Z

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(2, r.faceCount);
  TEST_ASSERT_TRUE(r.hasMean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[0], r.mean[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[1], r.mean[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[2] + SCALE[2], r.mean[2]);
}

void test_saved_calibration_gives_1g_and_the_true_angles() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasAccelCalibration = true;
  hw.store.accelCalibration = sensorCalibration();
  scriptTrue(hw.imu, TILTED[0], TILTED[1], TILTED[2], 200);

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.reading.accelCalibrated);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1.0f, r.reading.accelG);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, r.reading.pitchRaw);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, r.reading.rollRaw);
}

void test_wake_cycle_baseline_and_reading_use_the_calibration() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasAccelCalibration = true;
  hw.store.accelCalibration = sensorCalibration();
  scriptTrue(hw.imu, TILTED[0], TILTED[1], TILTED[2], 400);

  WakeResult w = runWakeCycle(WakeCause::PowerOn, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(hw.store.hasBaseline);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, hw.store.baseline.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, hw.store.baseline.rollDeg);
  TEST_ASSERT_TRUE(w.reading.accelCalibrated);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1.0f, w.reading.accelG);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w.reading.tiltDeg);
}

void test_face_capture_uses_the_raw_values_even_with_a_calibration_stored() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasAccelCalibration = true;
  hw.store.accelCalibration = {{0.3f, 0.3f, 0.3f}, {0.5f, 0.5f, 0.5f}};

  CalResult r = captureFace(hw, state, 4);  // +Z

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, OFFSET[2] + SCALE[2], r.mean[2]);
}

void test_face_capture_is_refused_while_the_board_moves() {
  FakeHardware hw;
  RtcState state;
  for (int i = 0; i < 100; ++i) {
    hw.imu.samples.push_back({0.0f, 0.0f, (i % 2) ? 1.02f : 0.98f, 0, 0, 0});
  }

  CalResult r = runCalCommand(CalStep::Face, CalFace::PosZ, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::Moving, r.error);
  TEST_ASSERT_EQUAL(0, r.faceCount);
}

void test_face_capture_is_refused_on_the_wrong_face() {
  FakeHardware hw;
  RtcState state;
  scriptTrue(hw.imu, 0, 0, 1, 100);  // +Z is up

  CalResult r = runCalCommand(CalStep::Face, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::WrongFace, r.error);
  TEST_ASSERT_TRUE(r.hasMean);  // shown, so the installer sees which way is up
  TEST_ASSERT_EQUAL(0, r.faceCount);
}

void test_face_capture_is_refused_upside_down() {
  FakeHardware hw;
  RtcState state;
  scriptTrue(hw.imu, 0, 0, -1, 100);  // -Z is up

  CalResult r = runCalCommand(CalStep::Face, CalFace::PosZ, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::WrongFace, r.error);
}

void test_save_is_refused_with_a_face_missing() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  for (int i = 0; i < 5; ++i) captureFace(hw, state, i);  // no -Z

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::MissingFaces, r.error);
  TEST_ASSERT_EQUAL(5, r.faceCount);
  TEST_ASSERT_EQUAL_HEX8(1u << static_cast<int>(CalFace::NegZ), r.missingFaces);
  TEST_ASSERT_FALSE(hw.store.hasAccelCalibration);
  TEST_ASSERT_TRUE(hw.store.hasBaseline);
}

void test_save_is_refused_when_a_scale_is_out_of_range() {
  FakeHardware hw;
  RtcState state;
  for (int i = 0; i < 4; ++i) captureFace(hw, state, i);
  scriptStill(hw.imu, 0.0f, 0.0f, 0.5f, 100);
  runCalCommand(CalStep::Face, CalFace::PosZ, state, testConfig(), hw.ports());
  scriptStill(hw.imu, 0.0f, 0.0f, -0.5f, 100);
  runCalCommand(CalStep::Face, CalFace::NegZ, state, testConfig(), hw.ports());  // Z scale 0.5

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::OutOfRange, r.error);
  TEST_ASSERT_FALSE(hw.store.hasAccelCalibration);
}

void test_save_is_refused_when_an_offset_is_out_of_range() {
  FakeHardware hw;
  RtcState state;
  for (int i = 0; i < 4; ++i) captureFace(hw, state, i);
  scriptStill(hw.imu, 0.0f, 0.0f, 1.4f, 100);
  runCalCommand(CalStep::Face, CalFace::PosZ, state, testConfig(), hw.ports());
  scriptStill(hw.imu, 0.0f, 0.0f, -0.6f, 100);
  runCalCommand(CalStep::Face, CalFace::NegZ, state, testConfig(), hw.ports());  // Z offset 0.4 g

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::OutOfRange, r.error);
  TEST_ASSERT_FALSE(hw.store.hasAccelCalibration);
}

void test_save_clears_the_baseline_the_faces_and_restarts_the_step_rule() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {1.0f, 2.0f};
  state.levels.hasPrevious = true;
  captureAllFaces(hw, state);

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.baselineCleared);
  TEST_ASSERT_FALSE(hw.store.hasBaseline);
  TEST_ASSERT_FALSE(state.levels.hasPrevious);
  TEST_ASSERT_EQUAL(0, state.cal.faces);
}

void test_save_reports_a_flash_error() {
  FakeHardware hw;
  RtcState state;
  hw.store.accelCalibrationSaveFails = true;
  hw.store.hasBaseline = true;
  captureAllFaces(hw, state);

  CalResult r = runCalCommand(CalStep::Save, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(CalError::Flash, r.error);
  TEST_ASSERT_TRUE(hw.store.hasBaseline);  // kept: it still matches the readings
}

void test_clear_removes_the_calibration_and_the_baseline() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasAccelCalibration = true;
  hw.store.accelCalibration = sensorCalibration();
  hw.store.hasBaseline = true;
  state.levels.hasPrevious = true;
  captureFace(hw, state, 0);

  CalResult r = runCalCommand(CalStep::Clear, CalFace::PosX, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.baselineCleared);
  TEST_ASSERT_FALSE(hw.store.hasAccelCalibration);
  TEST_ASSERT_FALSE(hw.store.hasBaseline);
  TEST_ASSERT_FALSE(state.levels.hasPrevious);
  TEST_ASSERT_EQUAL(0, state.cal.faces);
}

void test_status_shows_the_stored_calibration() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasAccelCalibration = true;
  hw.store.accelCalibration = sensorCalibration();

  CommandResult r = runCommand(Command::Status, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.hasAccelCalibration);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SCALE[2], r.accelCalibration.scale[2]);
}

void test_cal_arguments_are_parsed() {
  struct Case {
    const char* arg;
    CalStep step;
    CalFace face;
  };
  const Case ok[] = {{"+x", CalStep::Face, CalFace::PosX}, {"-x", CalStep::Face, CalFace::NegX},
                     {"+y", CalStep::Face, CalFace::PosY}, {"-y", CalStep::Face, CalFace::NegY},
                     {"+z", CalStep::Face, CalFace::PosZ}, {"-z", CalStep::Face, CalFace::NegZ},
                     {"save", CalStep::Save, CalFace::PosX}, {"clear", CalStep::Clear, CalFace::PosX}};
  for (const Case& c : ok) {
    CalStep step;
    CalFace face = CalFace::PosX;
    TEST_ASSERT_TRUE_MESSAGE(parseCalArg(c.arg, step, face), c.arg);
    TEST_ASSERT_EQUAL_MESSAGE(c.step, step, c.arg);
    if (c.step == CalStep::Face) TEST_ASSERT_EQUAL_MESSAGE(c.face, face, c.arg);
  }
  const char* bad[] = {"", "x", "+w", "+xx", "Save", "+X "};
  for (const char* arg : bad) {
    CalStep step;
    CalFace face;
    TEST_ASSERT_FALSE_MESSAGE(parseCalArg(arg, step, face), arg);
  }
}

void test_face_reply_line() {
  CalResult r;
  r.ok = true;
  r.step = CalStep::Face;
  r.face = CalFace::NegY;
  r.hasMean = true;
  r.mean[0] = 0.0123f;
  r.mean[1] = -1.0045f;
  r.mean[2] = 0.0501f;
  r.faceCount = 3;
  char line[200];

  formatCalReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@cal ok=1 step=-y faces=3 ax=0.0123 ay=-1.0045 az=0.0501", line);
}

void test_failed_face_reply_line_names_the_error() {
  CalResult r;
  r.ok = false;
  r.step = CalStep::Face;
  r.face = CalFace::PosZ;
  r.error = CalError::Moving;
  char line[200];

  formatCalReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@cal ok=0 step=+z faces=0 err=moving", line);
}

void test_save_reply_line() {
  CalResult r;
  r.ok = true;
  r.step = CalStep::Save;
  r.faceCount = 0;
  r.hasCalibration = true;
  r.calibration = {{0.02f, -0.03f, 0.05f}, {1.01f, 0.98f, 0.88f}};
  r.baselineCleared = true;
  char line[200];

  formatCalReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING(
      "@cal ok=1 step=save faces=0 ox=0.0200 oy=-0.0300 oz=0.0500 sx=1.0100 sy=0.9800 sz=0.8800 "
      "baseline=0",
      line);
}

void test_save_reply_line_lists_the_missing_faces() {
  CalResult r;
  r.ok = false;
  r.step = CalStep::Save;
  r.faceCount = 4;
  r.error = CalError::MissingFaces;
  r.missingFaces = (1u << static_cast<int>(CalFace::NegX)) | (1u << static_cast<int>(CalFace::PosZ));
  char line[200];

  formatCalReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@cal ok=0 step=save faces=4 err=missing_faces missing=-x,+z", line);
}

void test_clear_reply_line() {
  CalResult r;
  r.ok = true;
  r.step = CalStep::Clear;
  r.baselineCleared = true;
  char line[200];

  formatCalReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@cal ok=1 step=clear faces=0 baseline=0", line);
}

void test_read_reply_says_whether_the_reading_was_calibrated() {
  CommandResult r;
  r.ok = true;
  r.reading.accelCalibrated = true;
  char line[200];

  formatReadReply(r, line, sizeof line);

  TEST_ASSERT_NOT_NULL(std::strstr(line, " cal=1"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_calibration_leaves_readings_unchanged);
  RUN_TEST(test_six_faces_then_save_stores_the_offset_and_scale);
  RUN_TEST(test_faces_can_be_taken_in_any_order_and_retaken);
  RUN_TEST(test_face_reply_reports_the_average_and_the_faces_so_far);
  RUN_TEST(test_saved_calibration_gives_1g_and_the_true_angles);
  RUN_TEST(test_wake_cycle_baseline_and_reading_use_the_calibration);
  RUN_TEST(test_face_capture_uses_the_raw_values_even_with_a_calibration_stored);
  RUN_TEST(test_face_capture_is_refused_while_the_board_moves);
  RUN_TEST(test_face_capture_is_refused_on_the_wrong_face);
  RUN_TEST(test_face_capture_is_refused_upside_down);
  RUN_TEST(test_save_is_refused_with_a_face_missing);
  RUN_TEST(test_save_is_refused_when_a_scale_is_out_of_range);
  RUN_TEST(test_save_is_refused_when_an_offset_is_out_of_range);
  RUN_TEST(test_save_clears_the_baseline_the_faces_and_restarts_the_step_rule);
  RUN_TEST(test_save_reports_a_flash_error);
  RUN_TEST(test_clear_removes_the_calibration_and_the_baseline);
  RUN_TEST(test_status_shows_the_stored_calibration);
  RUN_TEST(test_cal_arguments_are_parsed);
  RUN_TEST(test_face_reply_line);
  RUN_TEST(test_failed_face_reply_line_names_the_error);
  RUN_TEST(test_save_reply_line);
  RUN_TEST(test_save_reply_line_lists_the_missing_faces);
  RUN_TEST(test_clear_reply_line);
  RUN_TEST(test_read_reply_says_whether_the_reading_was_calibrated);
  return UNITY_END();
}

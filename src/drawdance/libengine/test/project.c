// SPDX-License-Identifier: GPL-3.0-or-later
#include <dpcommon/common.h>
#include <dptest_engine.h>
#ifdef HAVE_PROJECT
#    include <dpcommon/file.h>
#    include <dpcommon/output.h>
#    include <dpengine/project.h>
#    include <dpmsg/message.h>


static bool remove_preexisting(TEST_PARAMS, const char *path)
{
    return OK(!DP_file_exists(path) || DP_file_remove(path),
              "Remove preexisting file %s", path)
        && INT_EQ_OK(DP_project_check(path), DP_PROJECT_CHECK_ERROR_OPEN,
                     "Removed project file can't be opened");
}

static bool dump_project(TEST_PARAMS, DP_Project *prj, const char *path)
{
    DP_Output *output = DP_file_output_new_from_path(path);
    if (!NOT_NULL_OK(output, "Open dump output %s", path)) {
        return false;
    }
    bool dump_ok = OK(DP_project_dump(prj, output), "Dump project");
    return OK(DP_output_free(output), "Close dump output") && dump_ok;
}

static bool dump_project_ok(TEST_PARAMS, DP_Project *prj,
                            const char *actual_path, const char *expected_path)
{
    return dump_project(TEST_ARGS, prj, actual_path)
        && FILE_EQ_OK(actual_path, expected_path, "Project dump matches");
}


static void project_basics(TEST_PARAMS)
{
    const char *path = "test/tmp/project_basics.dppr";
    remove_preexisting(TEST_ARGS, path);

    DP_Project *prj = DP_project_open(path, DP_PROJECT_OPEN_EXISTING);
    if (!NULL_OK(prj, "Opening nonexistent file with EXISTING flag fails")) {
        DP_project_close(prj);
    }

    prj = DP_project_open(path, 0);
    if (!NOT_NULL_OK(prj, "Open fresh project")) {
        return;
    }

    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump01_blank",
                    "test/data/project_basics_dump01_blank");

    OK(DP_project_close(prj), "Close project");

    INT_EQ_OK(DP_project_check(path), DP_PROJECT_USER_VERSION,
              "Project file checks out with version %d",
              DP_PROJECT_USER_VERSION);

    prj = DP_project_open(path, DP_PROJECT_OPEN_EXISTING);
    if (!NOT_NULL_OK(prj, "Reopen project with EXISTING flag")) {
        return;
    }

    INT_EQ_OK(DP_project_session_id(prj), 0LL, "No session open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump02_reopen",
                    "test/data/project_basics_dump02_reopen");

    OK(DP_project_session_open(prj, DP_PROJECT_SOURCE_BLANK, NULL, "dp:4.24.0"),
       "Open session");

    INT_EQ_OK(DP_project_session_id(prj), 1LL, "Session 1 open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump03_session1",
                    "test/data/project_basics_dump03_session1");

    OK(DP_project_close(prj), "Close project");

    prj = DP_project_open(path, 0);
    if (!NOT_NULL_OK(prj, "Reopen project")) {
        return;
    }

    INT_EQ_OK(DP_project_session_id(prj), 0LL, "No session open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump04_autoclose",
                    "test/data/project_basics_dump04_autoclose");

    OK(DP_project_session_open(prj, DP_PROJECT_SOURCE_FILE_OPEN,
                               "some/file.dppr", "dp:4.24.1"),
       "Open another session");

    INT_EQ_OK(DP_project_session_id(prj), 2LL, "Session 2 open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump05_session2",
                    "test/data/project_basics_dump05_session2");

    NOK(DP_project_session_open(prj, DP_PROJECT_SOURCE_SESSION_JOIN,
                                "drawpile://whatever/something", "dp:4.24.2"),
        "Trying to open session while another one is open fails");

    INT_EQ_OK(DP_project_session_id(prj), 2LL, "Session 2 open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump06_nodupe",
                    "test/data/project_basics_dump06_nodupe");

    INT_EQ_OK(DP_project_session_close(prj), 1, "Closing session");

    INT_EQ_OK(DP_project_session_id(prj), 0LL, "No session open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump07_close",
                    "test/data/project_basics_dump07_close");

    INT_EQ_OK(DP_project_session_close(prj), -1,
              "Closing session when none is open");
    INT_EQ_OK(DP_project_session_id(prj), 0LL, "No session open");

    OK(DP_project_close(prj), "Close project");

    prj = DP_project_open(path, DP_PROJECT_OPEN_TRUNCATE);
    if (!NOT_NULL_OK(prj, "Reopen project with TRUNCATE flag")) {
        return;
    }

    INT_EQ_OK(DP_project_session_id(prj), 0LL, "No session open");
    dump_project_ok(TEST_ARGS, prj, "test/tmp/project_basics_dump08_truncate",
                    "test/data/project_basics_dump08_truncate");

    OK(DP_project_close(prj), "Close project");

    INT_EQ_OK(DP_project_check(path), DP_PROJECT_USER_VERSION,
              "Truncated project file checks out with version %d",
              DP_PROJECT_USER_VERSION);
}

#endif

static void register_tests(REGISTER_PARAMS)
{
#ifdef HAVE_PROJECT
    REGISTER_TEST(project_basics);
#else
    SKIP_ALL("project file format not compiled in (ENABLE_PROJECT)");
#endif
}

int main(int argc, char **argv)
{
    return DP_test_main(argc, argv, register_tests, NULL);
}

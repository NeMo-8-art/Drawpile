// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"
#include <dpcommon/binary.h>
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>
#include <dpcommon/input.h>
#include <dpcommon/output.h>
#include <dpcommon/threading.h>
#include <dpmsg/message.h>
#include <sqlite3.h>


#define DP_PROJECT_STR(X)  #X
#define DP_PROJECT_XSTR(X) DP_PROJECT_STR(X)


typedef enum DP_ProjectStatement {
    DP_PROJECT_STATEMENT_MESSAGE_INSERT,
    DP_PROJECT_STATEMENT_SESSION_OPEN,
    DP_PROJECT_STATEMENT_SESSION_CLOSE,
    DP_PROJECT_STATEMENT_COUNT,
} DP_ProjectStatement;

struct DP_Project {
    sqlite3 *db;
    long long session_id;
    long long sequence_id;
    sqlite3_stmt *stmts[DP_PROJECT_STATEMENT_COUNT];
    unsigned char serialize_buffer[DP_MESSAGE_MAX_PAYLOAD_LENGTH];
};

static bool is_ok(int result)
{
    return result == SQLITE_OK;
}

static const char *db_error(sqlite3 *db)
{
    const char *message = db ? sqlite3_errmsg(db) : NULL;
    return message ? message : "unknown error";
}

static const char *fallback_db_error(sqlite3 *db, const char *errmsg)
{
    return errmsg ? errmsg : db_error(db);
}

static const char *prj_db_error(DP_Project *prj)
{
    return db_error(prj->db);
}


int DP_project_check(const char *path)
{
    DP_Input *input = DP_file_input_new_from_path(path);
    if (!input) {
        return DP_PROJECT_CHECK_ERROR_OPEN;
    }

    unsigned char buffer[72];
    bool error;
    size_t read = DP_input_read(input, buffer, sizeof(buffer), &error);
    DP_input_free(input);
    if (error) {
        return DP_PROJECT_CHECK_ERROR_READ;
    }

    if (read < sizeof(buffer)) {
        return DP_PROJECT_CHECK_ERROR_HEADER;
    }

    // "SQLite format 3\0" in ASCII
    unsigned char magic[] = {0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66,
                             0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00};
    if (memcmp(buffer, magic, sizeof(magic)) != 0) {
        return DP_PROJECT_CHECK_ERROR_MAGIC;
    }

    int application_id = DP_read_bigendian_int32(buffer + 68);
    if (application_id != DP_PROJECT_APPLICATION_ID) {
        return DP_PROJECT_CHECK_ERROR_APPLICATION_ID;
    }

    int user_version = DP_read_bigendian_int32(buffer + 60);
    if (user_version < 1) {
        return DP_PROJECT_CHECK_ERROR_USER_VERSION;
    }

    return user_version;
}


static sqlite3_stmt *prepare(sqlite3 *db, const char *sql, unsigned int flags)
{
    DP_debug("Prepare statement: %s", sql);
    sqlite3_stmt *stmt;
    int prepare_result = sqlite3_prepare_v3(db, sql, -1, flags, &stmt, NULL);
    if (is_ok(prepare_result)) {
        return stmt;
    }
    else {
        DP_error_set("Error %d preparing statement '%s': %s", prepare_result,
                     sql, db_error(db));
        return NULL;
    }
}

static bool exec_write_stmt(sqlite3 *db, const char *sql, const char *title)
{
    DP_ASSERT(db);
    DP_ASSERT(sql);
    DP_ASSERT(title);
    char *errmsg;
    int exec_result = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (is_ok(exec_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d %s: %s", exec_result, title,
                     fallback_db_error(db, errmsg));
        sqlite3_free(errmsg);
        return false;
    }
}

static bool exec_int_stmt(sqlite3 *db, const char *sql, int default_value,
                          int *out_value)
{
    DP_ASSERT(db);
    DP_ASSERT(sql);
    DP_ASSERT(out_value);

    sqlite3_stmt *stmt = prepare(db, sql, 0);
    if (!stmt) {
        return NULL;
    }

    bool value_ok;
    int step_result = sqlite3_step(stmt);
    if (step_result == SQLITE_DONE) {
        value_ok = true;
        *out_value = default_value;
    }
    else if (step_result == SQLITE_ROW) {
        if (sqlite3_column_count(stmt) >= 1) {
            value_ok = true;
            *out_value = sqlite3_column_int(stmt, 0);
        }
        else {
            value_ok = false;
            DP_error_set("Statement '%s' resulted in no columns", sql);
        }
    }
    else {
        value_ok = false;
        DP_error_set("Error %d executing statement '%s': %s", step_result, sql,
                     db_error(db));
    }

    sqlite3_finalize(stmt);
    return value_ok;
}

static bool is_empty_db(sqlite3 *db, bool *out_empty)
{
    int value;
    if (exec_int_stmt(db, "select 1 from sqlite_master limit 1", 0, &value)) {
        *out_empty = value == 0;
        return true;
    }
    else {
        return false;
    }
}

static bool init_header(sqlite3 *db)
{
    return exec_write_stmt(db,
                           "pragma application_id = " DP_PROJECT_XSTR(
                               DP_PROJECT_APPLICATION_ID),
                           "setting application_id")
        && exec_write_stmt(db,
                           "pragma user_version = " DP_PROJECT_XSTR(
                               DP_PROJECT_USER_VERSION),
                           "setting user_version");
}

static bool check_header(sqlite3 *db)
{
    int application_id, user_version;
    bool exec_ok =
        exec_int_stmt(db, "pragma application_id", -1, &application_id)
        && exec_int_stmt(db, "pragma user_version", -1, &user_version);
    if (exec_ok) {
        if (application_id != DP_PROJECT_APPLICATION_ID) {
            DP_error_set("File has incorrect application id %d",
                         application_id);
            return false;
        }
        else if (user_version != DP_PROJECT_USER_VERSION) {
            DP_error_set("File has unknown user version %d", user_version);
            return false;
        }
        else {
            return true;
        }
    }
    else {
        return false;
    }
}

static void try_rollback(sqlite3 *db)
{
    if (!sqlite3_get_autocommit(db)) {
        char *errmsg;
        int rollback_result = sqlite3_exec(db, "rollback", NULL, NULL, &errmsg);
        if (!is_ok(rollback_result)) {
            DP_warn("Error %d rolling back transaction: %s", rollback_result,
                    fallback_db_error(db, errmsg));
            sqlite3_free(errmsg);
        }
    }
}

static bool tx(sqlite3 *db, bool (*block)(sqlite3 *, void *), void *user)
{
    if (exec_write_stmt(db, "begin", "opening transaction")) {
        if (block(db, user)) {
            if (exec_write_stmt(db, "commit", "committing transaction")) {
                return true;
            }
            else {
                try_rollback(db);
                return false;
            }
        }
        else {
            try_rollback(db);
            return false;
        }
    }
    else {
        return false;
    }
}

static bool apply_migrations(sqlite3 *db, DP_UNUSED void *user)
{
    if (!exec_write_stmt(db,
                         "create table if not exists migrations (\n"
                         "    migration_id integer primary key not null)",
                         "creating migrations table")) {
        return false;
    }

    sqlite3_stmt *insert_migration_stmt = prepare(
        db, "insert or ignore into migrations (migration_id) values (?)", 0);
    if (!insert_migration_stmt) {
        return false;
    }

    const char *migrations[] = {
        // Migration 1: initial setup.
        "create table sessions (\n"
        "    session_id integer primary key not null,\n"
        "    source_type integer not null,\n"
        "    source_param text,\n"
        "    protocol text not null,\n"
        "    process_id integer not null\n,"
        "    opened_at real not null,\n"
        "    closed_at real,\n"
        "    thumbnail blob);\n"
        "create table messages (\n"
        "    session_id integer not null,\n"
        "    sequence_id integer not null,\n"
        "    recorded_at real not null,\n"
        "    type integer not null,\n"
        "    context_id integer not null,\n"
        "    body blob,\n"
        "    primary key (session_id, sequence_id)) without rowid;\n"
        "create table snapshots (\n"
        "    snapshot_id integer primary key not null,\n"
        "    session_id integer not null,\n"
        "    taken_at real not null);\n"
        "create table snapshot_messages (\n"
        "    snapshot_id integer not null,\n"
        "    sequence_id integer not null,\n"
        "    type integer not null,\n"
        "    context_id integer not null,\n"
        "    body blob,\n"
        "    primary key (snapshot_id, sequence_id)) without rowid",
    };

    bool result = true;
    for (int i = 0; i < (int)DP_ARRAY_LENGTH(migrations); ++i) {
        int bind_result = sqlite3_bind_int(insert_migration_stmt, 1, i + 1);
        if (!is_ok(bind_result)) {
            DP_error_set("Error %d binding migration insertion: %s",
                         bind_result, db_error(db));
            result = false;
            break;
        }

        if (sqlite3_step(insert_migration_stmt) != SQLITE_DONE) {
            DP_error_set("Error %d inserting migration: %s", bind_result,
                         db_error(db));
            result = false;
            break;
        }

        int changes = sqlite3_changes(db);
        int reset_result = sqlite3_reset(insert_migration_stmt);
        if (!is_ok(reset_result)) {
            DP_error_set("Error %d resetting migration insertion: %s",
                         bind_result, db_error(db));
            result = false;
            break;
        }

        if (changes > 0) {
            const char *sql = migrations[i];
            DP_debug("Executing migration %d", i + 1);
            if (!exec_write_stmt(db, sql, "executing migration")) {
                result = false;
                break;
            }
        }
    }

    sqlite3_finalize(insert_migration_stmt);
    return result;
}

DP_Project *DP_project_open(const char *path, unsigned int flags)
{
    int db_open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
    bool only_existing = flags & DP_PROJECT_OPEN_EXISTING;
    if (!only_existing) {
        db_open_flags |= SQLITE_OPEN_CREATE;
    }

    sqlite3 *db;
    int db_open_result = sqlite3_open_v2(path, &db, db_open_flags, NULL);
    if (!is_ok(db_open_result)) {
        DP_error_set("Error %d opening '%s': %s", db_open_result, path,
                     db_error(db));
        sqlite3_close(db);
        return NULL;
    }

    const char *main_schema = sqlite3_db_name(db, 0);
    if (sqlite3_db_readonly(db, main_schema) > 0) {
        DP_error_set("Error opening '%s': database is read-only", path);
        sqlite3_close(db);
        return NULL;
    }

    bool empty;
    if (flags & DP_PROJECT_OPEN_TRUNCATE) {
        // This is how you clear out the database according to the sqlite
        // manual. Lack of error checking is intentional, we'll notice later.
        sqlite3_table_column_metadata(db, NULL, "dummy_table", "dummy_column",
                                      NULL, NULL, NULL, NULL, NULL);
        sqlite3_db_config(db, SQLITE_DBCONFIG_RESET_DATABASE, 1, 0);
        sqlite3_exec(db, "vacuum", NULL, NULL, NULL);
        sqlite3_db_config(db, SQLITE_DBCONFIG_RESET_DATABASE, 0, 0);
        empty = true;
    }
    else if (!is_empty_db(db, &empty)) {
        sqlite3_close(db);
        return NULL;
    }

    bool init_ok = (!empty || init_header(db)) && check_header(db)
                && tx(db, apply_migrations, NULL);
    if (!init_ok) {
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_stmt *stmts[DP_PROJECT_STATEMENT_COUNT];
    const char *sqls[DP_PROJECT_STATEMENT_COUNT] = {
        [DP_PROJECT_STATEMENT_MESSAGE_INSERT] =
            "insert into messages (session_id, sequence_id, recorded_at, type, "
            "context_id, body) values (?, ?, unixepoch('subsec'), ? ,?, ?)",
        [DP_PROJECT_STATEMENT_SESSION_OPEN] =
            "insert into sessions (source_type, source_param, protocol, "
            "process_id, opened_at) values (?, ?, ?, ?, unixepoch('subsec'))",
        [DP_PROJECT_STATEMENT_SESSION_CLOSE] =
            "update sessions set closed_at = unixepoch('subsec') "
            "where session_id = ?",
    };
    for (int i = 0; i < DP_PROJECT_STATEMENT_COUNT; ++i) {
        sqlite3_stmt *stmt = prepare(db, sqls[i], SQLITE_PREPARE_PERSISTENT);
        if (stmt) {
            stmts[i] = stmt;
        }
        else {
            for (int j = 0; j < i; ++j) {
                sqlite3_finalize(stmts[i]);
            }
            sqlite3_close(db);
            return NULL;
        }
    }

    DP_Project *prj = DP_malloc(sizeof(*prj));
    prj->db = db;
    prj->session_id = 0LL;
    prj->sequence_id = 0LL;
    memcpy(prj->stmts, stmts, sizeof(stmts));
    return prj;
}

bool DP_project_close(DP_Project *prj)
{
    if (!prj) {
        DP_error_set("Project is null");
        return false;
    }

    if (DP_project_session_close(prj) == 0) {
        DP_warn("Close project: %s", DP_error());
    }

    for (int i = 0; i < DP_PROJECT_STATEMENT_COUNT; ++i) {
        sqlite3_finalize(prj->stmts[i]);
    }
    int close_result = sqlite3_close(prj->db);
    DP_free(prj);

    if (is_ok(close_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d closing project", close_result);
        return false;
    }
}


static const char *ps_title(DP_ProjectStatement ps)
{
    switch (ps) {
    case DP_PROJECT_STATEMENT_MESSAGE_INSERT:
        return "message insert";
    case DP_PROJECT_STATEMENT_SESSION_OPEN:
        return "session open";
    case DP_PROJECT_STATEMENT_SESSION_CLOSE:
        return "session close";
    case DP_PROJECT_STATEMENT_COUNT:
        break;
    }
    return "unknown";
}

static bool ps_bind_int(DP_Project *prj, DP_ProjectStatement ps, int param,
                        int value)
{
    int bind_result = sqlite3_bind_int(prj->stmts[ps], param, value);
    if (is_ok(bind_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d binding int parameter %d to %s: %s", bind_result,
                     param, ps_title(ps), prj_db_error(prj));
        return false;
    }
}

static bool ps_bind_int64(DP_Project *prj, DP_ProjectStatement ps, int param,
                          long long value)
{
    int bind_result = sqlite3_bind_int64(prj->stmts[ps], param, value);
    if (is_ok(bind_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d binding int64 parameter %d to %s: %s",
                     bind_result, param, ps_title(ps), prj_db_error(prj));
        return false;
    }
}

static bool ps_bind_blob(DP_Project *prj, DP_ProjectStatement ps, int param,
                         const void *value, size_t length)
{
    int bind_result = sqlite3_bind_blob64(prj->stmts[ps], param, value, length,
                                          SQLITE_STATIC);
    if (is_ok(bind_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d binding blob parameter %d to %s: %s",
                     bind_result, param, ps_title(ps), prj_db_error(prj));
        return false;
    }
}

static bool ps_bind_text(DP_Project *prj, DP_ProjectStatement ps, int param,
                         const char *value)
{
    int bind_result =
        sqlite3_bind_text(prj->stmts[ps], param, value, -1, SQLITE_STATIC);
    if (is_ok(bind_result)) {
        return true;
    }
    else {
        DP_error_set("Error %d binding blob parameter %d to %s: %s",
                     bind_result, param, ps_title(ps), prj_db_error(prj));
        return false;
    }
}

bool ps_exec_write(DP_Project *prj, DP_ProjectStatement ps,
                   long long *out_insert_rowid)
{
    sqlite3_stmt *stmt = prj->stmts[ps];
    int step_result = sqlite3_step(stmt);
    if (step_result == SQLITE_DONE) {
        int reset_result = sqlite3_reset(stmt);
        if (is_ok(reset_result)) {
            if (out_insert_rowid) {
                *out_insert_rowid = sqlite3_last_insert_rowid(prj->db);
            }
            return true;
        }
        else {
            DP_error_set("Error %d resetting %s: %s", reset_result,
                         ps_title(ps), prj_db_error(prj));
            return false;
        }
    }
    else {
        const char *title = ps_title(ps);
        DP_error_set("Error %d executing %s: %s", step_result, title,
                     prj_db_error(prj));
        int reset_result = sqlite3_reset(stmt);
        if (!is_ok(reset_result)) {
            DP_warn("Error %d resetting failed %s: %s", reset_result, title,
                    prj_db_error(prj));
        }
        return false;
    }
}

void ps_clear_bindings(DP_Project *prj, DP_ProjectStatement ps)
{
    int clear_result = sqlite3_clear_bindings(prj->stmts[ps]);
    if (!is_ok(clear_result)) {
        DP_warn("Error clearing bindings on %s: %s", ps_title(ps),
                prj_db_error(prj));
    }
}


long long DP_project_session_id(DP_Project *prj)
{
    DP_ASSERT(prj);
    return prj->session_id;
}

bool DP_project_session_open(DP_Project *prj, DP_ProjectSourceType source_type,
                             const char *source_param, const char *protocol)
{
    DP_ASSERT(prj);
    DP_ASSERT(protocol);
    if (prj->session_id == 0LL) {
        DP_debug("Opening session source %d %s, protocol %s", (int)source_type,
                 source_param ? source_param : "<NULL>", protocol);
        prj->sequence_id = 0LL;
        bool write_ok =
            ps_bind_int(prj, DP_PROJECT_STATEMENT_SESSION_OPEN, 1,
                        (int)source_type)
            && ps_bind_text(prj, DP_PROJECT_STATEMENT_SESSION_OPEN, 2,
                            source_param)
            && ps_bind_text(prj, DP_PROJECT_STATEMENT_SESSION_OPEN, 3, protocol)
            && ps_bind_int64(prj, DP_PROJECT_STATEMENT_SESSION_OPEN, 4,
                             DP_process_current_id())
            && ps_exec_write(prj, DP_PROJECT_STATEMENT_SESSION_OPEN,
                             &prj->session_id);
        ps_clear_bindings(prj, DP_PROJECT_STATEMENT_SESSION_OPEN);
        return write_ok;
    }
    else {
        DP_error_set("Error opening session: session %lld already open",
                     prj->session_id);
        return false;
    }
}

int DP_project_session_close(DP_Project *prj)
{
    DP_ASSERT(prj);
    long long session_id = prj->session_id;
    if (session_id == 0LL) {
        return -1;
    }
    else {
        prj->session_id = 0LL;
        bool update_ok =
            ps_bind_int64(prj, DP_PROJECT_STATEMENT_SESSION_CLOSE, 1,
                          session_id)
            && ps_exec_write(prj, DP_PROJECT_STATEMENT_SESSION_CLOSE, NULL);
        return update_ok ? 1 : 0;
    }
}


unsigned char *get_serialize_buffer(void *user, DP_UNUSED size_t length)
{
    DP_ASSERT(length <= DP_MESSAGE_MAX_PAYLOAD_LENGTH);
    DP_Project *prj = user;
    return prj->serialize_buffer;
}

bool DP_project_message_record(DP_Project *prj, DP_Message *msg)
{
    DP_ASSERT(prj);
    DP_ASSERT(msg);

    long long session_id = prj->session_id;
    if (session_id == 0) {
        DP_error_set("No open session");
        return false;
    }

    size_t length = DP_message_serialize_body(msg, get_serialize_buffer, prj);
    if (length == 0) {
        return false;
    }

    bool write_ok =
        ps_bind_int64(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, 1, session_id)
        && ps_bind_int64(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, 2,
                         ++prj->sequence_id)
        && ps_bind_int(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, 3,
                       (int)DP_message_type(msg))
        && ps_bind_int64(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, 4,
                         DP_message_context_id(msg))
        && ps_bind_blob(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, 5,
                        prj->serialize_buffer, length)
        && ps_exec_write(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT, NULL);
    ps_clear_bindings(prj, DP_PROJECT_STATEMENT_MESSAGE_INSERT);
    return write_ok;
}


static int dump_rows(void *user, int ncols, char **values, char **names)
{
    DP_Output *output = ((void **)user)[0];
    int *row_ptr = ((void **)user)[1];
    int row = *row_ptr;

    if (row < 0) {
        return SQLITE_ERROR;
    }

    if (row == 0) {
        for (int i = 0; i < ncols; ++i) {
            const char *name = names[i];
            const char *suffix = i < ncols - 1 ? "," : "\n";
            bool output_ok = name && name[0] != '\0'
                               ? DP_output_format(output, "%s%s", name, suffix)
                               : DP_output_format(output, "?%d%s", i, suffix);
            if (!output_ok) {
                *row_ptr = -1;
                return SQLITE_ERROR;
            }
        }
    }

    for (int i = 0; i < ncols; ++i) {
        const char *value = values[i];
        const char *suffix = i < ncols - 1 ? "," : "\n";
        bool output_ok = value
                           ? DP_output_format(output, "'%s'%s", value, suffix)
                           : DP_output_format(output, "NULL%s", suffix);
        if (!output_ok) {
            *row_ptr = -1;
            return SQLITE_ERROR;
        }
    }

    *row_ptr = row + 1;
    return SQLITE_OK;
}

static bool dump_query(DP_Project *prj, DP_Output *output, const char *sql)
{
    if (DP_output_format(output, "\n--- %s\n", sql)) {
        char *errmsg;
        int row = 0;
        int exec_result = sqlite3_exec(prj->db, sql, dump_rows,
                                       (void *[]){output, &row}, &errmsg);
        if (row < 0) {
            sqlite3_free(errmsg);
            return false;
        }
        else if (!is_ok(exec_result)) {
            bool output_ok =
                DP_output_format(output, "\n*** error: %s\n",
                                 fallback_db_error(prj->db, errmsg));
            sqlite3_free(errmsg);
            return output_ok;
        }
        else {
            return true;
        }
    }
    else {
        return false;
    }
}

bool DP_project_dump(DP_Project *prj, DP_Output *output)
{
    DP_ASSERT(prj);
    DP_ASSERT(output);
    return DP_OUTPUT_PRINT_LITERAL(output, "begin project dump\n")
        && dump_query(prj, output, "pragma application_id")
        && dump_query(prj, output, "pragma user_version")
        && dump_query(
               prj, output,
               "select migration_id from migrations order by migration_id")
        && dump_query(
               prj, output,
               "select session_id, source_type, source_param, protocol, "
               "case when closed_at is null then 'open' else 'closed' end "
               "as status from sessions order by session_id")
        && DP_OUTPUT_PRINT_LITERAL(output, "\nend project dump\n");
}

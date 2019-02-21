
#include "dbp.h"

int dbp_open ( const char *conninfo,PGconn **conn ) {
    if ( PQstatus ( *conn ) == CONNECTION_OK ) {
        return 1;
    }
    *conn = PQconnectdb ( conninfo );
    if ( PQstatus ( *conn ) != CONNECTION_OK ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): %s\n", F, PQerrorMessage ( *conn ) );
#endif
        PQfinish ( *conn );
        return 0;
    }
    return 1;
}

int dbp_wait ( const char *conninfo ) {
    while ( 1 ) {
        PGPing r=PQping ( conninfo );
        switch ( r ) {
        case PQPING_OK:
            return 1;
            break;
        case PQPING_REJECT:
#ifdef MODE_DEBUG
            fprintf ( stderr, "%s(): %s\n", F, "warning: database server is not ready" );
#endif
            break;
        case PQPING_NO_RESPONSE:
#ifdef MODE_DEBUG
            fprintf ( stderr, "%s(): %s\n", F, "warning: no response from database server" );
#endif
            break;
        case PQPING_NO_ATTEMPT:
#ifdef MODE_DEBUG
            fprintf ( stderr, "%s(): %s\n", F, "failed to ping database server" );
#endif
            return 0;
        }
        delayUsIdle ( DBP_WAIT_DELAY_US );
    }
}

int dbp_getConnAlt ( PGconn **conn_out, int *close, PGconn * conn, const char * conninfo ) {
    *close=0;
    if ( conn != NULL && conninfo != NULL ) {
        putsde ( "db_conn xor db_conninfo expected\n" );
        return 0;
    }
    if ( conninfo != NULL ) {
        PGconn *_conn;
        if ( !dbp_open ( conninfo, &_conn ) ) {
            putsde ( "failed\n" );
            return 0;
        }
        *conn_out=_conn;
        *close=1;
    } else {
        *conn_out = conn;
    }
    return 1;
}
/*
 * for command returning NO data
*/

int dbp_cmd ( PGconn *conn, char *q ) {
    int out = 0;
    PGresult *r = PQexec ( conn, q );
    if ( r == NULL ) {
        printde ( "%s: no result: %s\n", q, PQerrorMessage ( conn ) );
        return out;
    }
    if ( PQresultStatus ( r ) == PGRES_COMMAND_OK ) {
        out = 1;
    }
    if ( out == 0 ) {
        printde ( "%s: command is not ok\n", q );
    }
    PQclear ( r );
    return out;
}

/*
 *  for command returning data
*/

int dbp_exec ( PGresult **r , PGconn *conn, const char *q ) {
    *r = PQexec ( conn, q );
    if ( PQresultStatus ( *r ) != PGRES_TUPLES_OK ) {
        printde ( "%s: tuples are not ok: %s\n", q, PQerrorMessage ( conn ) );
        PQclear ( *r );
        return 0;
    }
    return 1;
}
//for use with select count(*)

int dbp_getInt ( int *item, PGconn *conn, char *q ) {
    PGresult *r = PQexec ( conn, q );
    if ( r == NULL ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): %s: no result: %s\n", F, q, PQerrorMessage ( conn ) );
#endif
        return 0;
    }
    if ( PQresultStatus ( r ) != PGRES_TUPLES_OK ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): %s: tuples are not ok\n", F, q );
#endif
        PQclear ( r );
        return 0;
    }
    int n = PQntuples ( r );
    if ( n != 1 ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): %s: tuples: need only one, but %d given\n", F, q, n );
#endif
        PQclear ( r );
        return 0;
    }
    *item = atoi ( PQgetvalue ( r, 0, 0 ) );
    PQclear ( r );
    return 1;
}

int dbp_saveTableFieldInt ( const char * table, const char *field, int id, int value, const char * schema,  PGconn *conn, const char* conninfo ) {
    PGconn *db;
    int close=0;
    if ( !dbp_getConnAlt ( &db, &close, conn, conninfo ) ) {
        putsde ( "DB connection failed\n" );
        return 0;
    }
    char q[LINE_SIZE];
    snprintf ( q, sizeof q, "update '%s'.'%s' set '%s'=%d where id=%d", schema, table, field, value, id );
    if ( !dbp_cmd ( db, q ) ) {
        putsde ( "failed to save\n" );
        if ( close ) PQfinish ( db );
        return 0;
    }
    if ( close ) PQfinish ( db );
    return 1;
}

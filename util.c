#include "main.h"

void freeChannel ( Channel * item ) {
    freeSocketFd ( &item->sock_fd );
    freeMutex ( &item->mutex );
    free ( item );
}

void freeChannelList ( ChannelLList * list ) {
    Channel *item = list->top, *temp;
    while ( item != NULL ) {
        temp = item;
        item = item->next;
        freeChannel ( temp );
    }
    list->top = NULL;
    list->last = NULL;
    list->length = 0;
}

int checkProg ( const Prog *item ) {
    if ( item->interval.tv_sec < 0 || item->interval.tv_nsec < 0 ) {
        fprintf ( stderr, "%s(): negative interval where prog id = %d\n", F,item->id );
        return 0;
    }
    if ( item->kind != LOG_KIND_FTS ) {
        fprintf ( stderr, "%s(): bad kind where prog id = %d\n", F,item->id );
        return 0;
    }
    if ( item->max_rows < 0 ) {
        fprintf ( stderr, "%s(): negative max_rows where prog id = %d\n", F,item->id );
        return 0;
    }
    return 1;
}

int checkChannel ( const Channel *item ) {
    int success=1;
    return success;
}

char * getStateStr ( int state ) {
    switch ( state ) {
    case OFF:
        return "OFF";
    case INIT:
        return "INIT";
    case RUN:
        return "RUN";
    case WAIT:
        return "WAIT";
    case READ:
        return "READ";
    case SAVE:
        return "SAVE";
    case LOG_KIND_FTS:
        return LOG_KIND_FTS_STR;
    case DISABLE:
        return "DISABLE";
    case UNDEFINED:
        return "UNDEFINED";
    case FAILURE:
        return "FAILURE";
    }
    return "\0";
}

int pp_clearFTS ( int id, PGconn *db_conn ) {
    char q[LINE_SIZE];
    snprintf ( q, sizeof q, "delete from log.v_real where id = %d", id );
    if ( !dbp_cmd ( db_conn, q ) ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): log function failed\n", F );
#endif
        return 0;
    }
    return 1;
}

int pp_saveFTS ( FTS *item, Prog *prog, PGconn *db_conn ) {
    if ( prog->max_rows <= 0 ) {
        return 0;
    }
    if ( item->state != 1 ) {
        return 0;
    }
    char q[LINE_SIZE];
    snprintf ( q, sizeof q, "select log.do_real(%d,%f,%u,%ld)", prog->id, item->value, prog->max_rows, item->tm.tv_sec );
    PGresult *r;
    if ( !dbp_exec ( &r, db_conn, q ) ) {
#ifdef MODE_DEBUG
        fprintf ( stderr, "%s(): log function failed\n", F );
#endif
        return 0;
    }
    PQclear ( r );
    return 1;
}

void progEnable ( Prog *item ) {
    item->state=INIT;
}
void progDisable ( Prog *item ) {
    item->state=DISABLE;
}
void progStop ( Prog *item ) {
    item->state=DISABLE;;
}

void progControl ( Prog *item, Sensor *sensor, PGconn *db_conn ) {
    switch ( item->state ) {
    case INIT:
        tonSetInterval ( item->interval, &item->tmr );
        tonReset ( &item->tmr );
        if ( item->clear ) {
            if ( item->kind == LOG_KIND_FTS ) {
                pp_clearFTS ( sensor->input.id, db_conn );
            } else {
                putsde ( "unknown kind\n" );
            }
        }
        item->state = WAIT;
        break;
    case WAIT:
        if ( ton ( &item->tmr ) ) {
            item->state = READ;
        }
        break;
    case READ:
        switch ( sensorRead ( sensor ) ) {
        case ACP_RETURN_SUCCESS:
            item->state = SAVE;puts("success");
            break;
        case ACP_RETURN_FAILURE:
            item->state = WAIT;puts("failure");
            break;
        }
        break;
    case SAVE:
        pp_saveFTS ( &sensor->input, item, db_conn );
        item->state = WAIT;
        break;
    case DISABLE:
        item->state = OFF;
        break;
    case OFF:
        break;
    case FAILURE:
        break;
    default:
        item->state = FAILURE;
        break;
    }
}

int bufCatProgInfo ( Channel *item, ACPResponse *response ) {
    if ( lockMutex ( &item->mutex ) ) {
        char q[LINE_SIZE];
        char *state = getStateStr ( item->prog.state );
        char *kind = getStateStr ( item->prog.kind );
        struct timespec tm_rest = tonTimeRest ( &item->prog.tmr );
        snprintf ( q, sizeof q, "%d" CDS "%ld" CDS "%ld" CDS "%zd" CDS "%s" CDS "%d" CDS "%s" CDS "%ld" CDS "%ld" RDS,
                   item->id,
                   item->prog.interval.tv_sec,
                   item->prog.interval.tv_nsec,
                   item->prog.max_rows,
                   kind,
                   item->prog.clear,
                   state,
                   tm_rest.tv_sec,
                   tm_rest.tv_nsec
                 );
        unlockMutex ( &item->mutex );
        return acp_responseStrCat ( response, q );
    }
    return 0;
}

int bufCatProgEnabled ( Channel *item, ACPResponse *response ) {
    if ( lockMutex ( &item->mutex ) ) {
        char q[LINE_SIZE];
        int enabled;
        switch ( item->prog.state ) {
        case OFF:
        case FAILURE:
            enabled=0;
            break;
        default:
            enabled=1;
            break;
        }
        snprintf ( q, sizeof q, "%d" CDS "%d" RDS,
                   item->id,
                   enabled
                 );
        unlockMutex ( &item->mutex );
        return acp_responseStrCat ( response, q );
    }
    return 0;
}

int bufCatFTS ( Channel *item, ACPResponse *response ) {
    FTS result = {.id=-1, .tm.tv_sec=0, .tm.tv_nsec=0, .value=0.0, .state=0};
    if ( lockMutex ( &item->mutex ) ) {
        result = item->sensor.input;
        unlockMutex ( &item->mutex );
    }
    return acp_responseFTSCat ( result.id, result.value, result.tm, result.state, response );
}

void printData ( ACPResponse *response ) {
    char q[LINE_SIZE];
    snprintf ( q, sizeof q, "CONFIG_FILE: %s\n", CONFIG_FILE );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "port: %d\n", sock_port );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "working_thread.cycle_duration: %ld s, %ld ns\n", working_thread.cycle_duration.tv_sec, working_thread.cycle_duration.tv_nsec );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "db_prog_path: %s\n", db_prog_path );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "db_log_path: %s\n", db_log_path );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "app_state: %s\n", getAppState ( app_state ) );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "PID: %d\n", getpid() );
    SEND_STR ( q )

    SEND_STR ( "+-----------------------------------+\n" )
    SEND_STR ( "|              Channel              |\n" )
    SEND_STR ( "+-----------+-----------+-----------+\n" )
    SEND_STR ( "|     id    |  prog_id  |   save    |\n" )
    SEND_STR ( "+-----------+-----------+-----------+\n" )
    FOREACH_LLIST ( item, &working_thread.channel_list, Channel ) {
        snprintf ( q, sizeof q, "|%11d|%11d|%11d|\n",
                   item->id,
                   item->prog.id,
                   item->save
                 );
        SEND_STR ( q )
    }
    SEND_STR ( "+-----------++-----------+-----------+\n" )

    SEND_STR ( "+-----------------------------------------------------------------------------------------------+\n" )
    SEND_STR ( "|                                          Channel                                              |\n" )
    SEND_STR ( "+-----------+-----------------------------------------------------------------------------------+\n" )
    SEND_STR ( "|           |                                  Sensor                                           |\n" )
    SEND_STR ( "|           +-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )
    SEND_STR ( "|    id     |    id     |  peer_id  | remote_id |   value   |    sec    |    nsec   |   state   |\n" )
    SEND_STR ( "+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )
    FOREACH_LLIST ( item, &working_thread.channel_list, Channel ) {
        snprintf ( q, sizeof q, "|%11d|%11d|%11s|%11d|%11.3f|%11ld|%11ld|%11d|\n",
                   item->id,
                   item->sensor.remote_channel.id,
                   item->sensor.remote_channel.peer.id,
                   item->sensor.remote_channel.channel_id,
                   item->sensor.input.value,
                   item->sensor.input.tm.tv_sec,
                   item->sensor.input.tm.tv_nsec,
                   item->sensor.input.state
                 );
        SEND_STR ( q )
    }
    SEND_STR ( "+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )

    SEND_STR ( "+-----------------------------------------------------------------------------------------------------------------------+\n" )
    SEND_STR ( "|                                                       Channel                                                         |\n" )
    SEND_STR ( "+-----------+-----------------------------------------------------------------------------------------------------------+\n" )
    SEND_STR ( "|           |                                                    Prog                                                   |\n" )
    SEND_STR ( "|           +-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )
    SEND_STR ( "|    id     |    id     |interval_s |interval_ns|  max_rows |   clear   |   kind    |   state   |  trest_s  |  trest_ns |\n" )
    SEND_STR ( "+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )
    FOREACH_LLIST ( item, &working_thread.channel_list, Channel ) {
        char *state=getStateStr ( item->prog.state );
        char *kind=getStateStr ( item->prog.kind );
        struct timespec tm_rest=tonTimeRest ( &item->prog.tmr );
        snprintf ( q, sizeof q, "|%11d|%11d|%11ld|%11ld|%11d|%11d|%11s|%11s|%11ld|%11ld|\n",
                   item->id,
                   item->prog.id,
                   item->prog.interval.tv_sec,
                   item->prog.interval.tv_nsec,
                   item->prog.max_rows,
                   item->prog.clear,
                   kind,
                   state,
                   tm_rest.tv_sec,
                   tm_rest.tv_nsec
                 );
        SEND_STR ( q )
    }
    SEND_STR_L ( "+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------+\n" )


}

void printHelp ( ACPResponse *response ) {
    char q[LINE_SIZE];
    SEND_STR ( "COMMAND LIST\n" )
    snprintf ( q, sizeof q, "%s\tput process into active mode; process will read configuration\n", ACP_CMD_APP_START );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tput process into standby mode; all running programs will be stopped\n", ACP_CMD_APP_STOP );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tfirst stop and then start process\n", ACP_CMD_APP_RESET );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tterminate process\n", ACP_CMD_APP_EXIT );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget state of process; response: B - process is in active mode, I - process is in standby mode\n", ACP_CMD_APP_PING );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget some variable's values; response will be packed into multiple packets\n", ACP_CMD_APP_PRINT );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget this help; response will be packed into multiple packets\n", ACP_CMD_APP_HELP );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tload channel into RAM and start its execution; channel id expected\n", ACP_CMD_CHANNEL_START );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tunload channel from RAM; channel id expected\n", ACP_CMD_CHANNEL_STOP );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tunload channel from RAM, after that load it; channel id expected\n", ACP_CMD_CHANNEL_RESET );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tenable running channel; channel id expected\n", ACP_CMD_CHANNEL_ENABLE );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tdisable running channel; channel id expected\n", ACP_CMD_CHANNEL_DISABLE );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget channel state (1-enabled, 0-disabled); channel id expected\n", ACP_CMD_CHANNEL_GET_ENABLED );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget channel info; channel id expected\n", ACP_CMD_CHANNEL_GET_INFO );
    SEND_STR ( q )
    snprintf ( q, sizeof q, "%s\tget channel last saved value; channel id expected\n", ACP_CMD_GET_FTS );
    SEND_STR_L ( q )
}

/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "discover.h"
#include "player.h"
#include "util.h"
#include "constants.h"
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

static void resolve_cb(AvahiServiceResolver *r, AvahiIfIndex iface,
                       AvahiProtocol protocol, AvahiResolverEvent event,
                       const char *name, const char *type, const char *domain,
                       const char *host_name, const AvahiAddress *address,
                       uint16_t port, AvahiStringList *txt,
                       AvahiLookupResultFlags flags, void *userdata)
{
    (void)iface; (void)protocol; (void)name; (void)type; (void)domain;
    (void)host_name; (void)port; (void)txt; (void)flags;

    DiscoveryState *state = userdata;

    if (event == AVAHI_RESOLVER_FOUND && address->proto == AVAHI_PROTO_INET) {
        char ip[AVAHI_ADDRESS_STR_MAX];
        avahi_address_snprint(ip, sizeof(ip), address);

        /* Check if already discovered */
        pthread_mutex_lock(&state->lock);
        bool exists = false;
        for (int i = 0; i < state->count; i++) {
            if (strcmp(state->players[i]->host_name, ip) == 0) {
                exists = true;
                break;
            }
        }
        pthread_mutex_unlock(&state->lock);

        if (!exists) {
            BlusoundPlayer *player = player_create(ip, name);
            pthread_mutex_lock(&state->lock);
            if (state->count >= state->capacity) {
                state->capacity = state->capacity ? state->capacity * 2 : 4;
                state->players = realloc(state->players,
                                         state->capacity * sizeof(BlusoundPlayer *));
            }
            state->players[state->count++] = player;
            pthread_mutex_unlock(&state->lock);
        }
    }
    avahi_service_resolver_free(r);
}

static void browse_cb(AvahiServiceBrowser *b, AvahiIfIndex iface,
                      AvahiProtocol protocol, AvahiBrowserEvent event,
                      const char *name, const char *type, const char *domain,
                      AvahiLookupResultFlags flags, void *userdata)
{
    (void)b; (void)flags;
    DiscoveryState *state = userdata;

    if (event == AVAHI_BROWSER_NEW) {
        AvahiClient *client = avahi_service_browser_get_client(b);
        avahi_service_resolver_new(client, iface, protocol, name, type, domain,
                                   AVAHI_PROTO_INET, 0, resolve_cb, state);
    }
}

static void *discovery_thread(void *arg)
{
    DiscoveryState *state = arg;
    AvahiSimplePoll *poll = avahi_simple_poll_new();
    if (!poll) return NULL;

    int error;
    AvahiClient *client = avahi_client_new(avahi_simple_poll_get(poll), 0,
                                           NULL, NULL, &error);
    if (!client) {
        avahi_simple_poll_free(poll);
        return NULL;
    }

    AvahiServiceBrowser *browser = avahi_service_browser_new(
        client, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
        MDNS_SERVICE_TYPE, NULL, 0, browse_cb, state);

    if (!browser) {
        avahi_client_free(client);
        avahi_simple_poll_free(poll);
        return NULL;
    }

    while (state->running) {
        avahi_simple_poll_iterate(poll, 1000);
    }

    avahi_service_browser_free(browser);
    avahi_client_free(client);
    avahi_simple_poll_free(poll);
    return NULL;
}

DiscoveryState *discover_start(void)
{
    DiscoveryState *state = calloc(1, sizeof(DiscoveryState));
    pthread_mutex_init(&state->lock, NULL);
    state->running = true;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, discovery_thread, state);
    pthread_attr_destroy(&attr);

    return state;
}

void discover_stop(DiscoveryState *state)
{
    if (!state) return;
    state->running = false;
    usleep(100000); /* Give thread time to exit poll loop */
    pthread_mutex_lock(&state->lock);
    for (int i = 0; i < state->count; i++) {
        player_destroy(state->players[i]);
    }
    free(state->players);
    pthread_mutex_unlock(&state->lock);
    pthread_mutex_destroy(&state->lock);
    free(state);
}

int discover_player_count(DiscoveryState *state)
{
    pthread_mutex_lock(&state->lock);
    int n = state->count;
    pthread_mutex_unlock(&state->lock);
    return n;
}

int discover_get_players(DiscoveryState *state, BlusoundPlayer **dst, int max)
{
    pthread_mutex_lock(&state->lock);
    int n = int_min(state->count, max);
    for (int i = 0; i < n; i++) dst[i] = state->players[i];
    pthread_mutex_unlock(&state->lock);
    return n;
}

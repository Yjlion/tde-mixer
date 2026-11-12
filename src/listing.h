/*
 * tde-mixer - per-application volume mixer for the Trinity Desktop Environment
 *
 * Copyright (C) 2026 tde-mixer developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef LISTING_H
#define LISTING_H

class AudioBackend;

/** waits until the backend is connected and its model is complete */
bool waitForModel(AudioBackend *backend, int timeoutMs = 5000);
/** processes events for @p ms milliseconds */
void processEventsFor(int ms);

/* the command line actions of tde-mixer; return the exit code */
int runListing(AudioBackend *backend);
int runSetVolume(AudioBackend *backend, int percent);
int runToggleMute(AudioBackend *backend);

#endif

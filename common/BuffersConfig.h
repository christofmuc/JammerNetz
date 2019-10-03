/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

// At 48KHz sampling rate, a buffer of size 128 samples is only 2.66ms long, we send at 375 Hz.

// On the cable line, we have measured package jitter in the order of +10 to -5 milliseconds
// We have also seen drops of up to 3 packets in a row once per second

const int SAMPLE_BUFFER_SIZE = 128;
const int SAMPLE_RATE = 48000;
const int SAMPLES_PER_MILLISECOND = 48000 / 1000;

// The mixer only mixes when at least that amount of packages is available. This is the amount of package delta between different clients sending
// 20 ms sever jitter buffer
const int SERVER_INCOMING_JITTER_BUFFER = 10 * SAMPLES_PER_MILLISECOND / SAMPLE_BUFFER_SIZE;

// We cannot wait forever with mixing - if this number is reached for one client, we will send out anyway, even if one of clients hasn't delivered INCOMING_JITTER_BUFFER packages
const int SERVER_INCOMING_MAXIMUM_BUFFER = 30 * SAMPLES_PER_MILLISECOND / SAMPLE_BUFFER_SIZE;

// When a client connects anew and sends a first package, fill this number of fake packages into his queue so he has a chance to not get kicked out of the mixer again
const int BUFFER_PREFILL_ON_CONNECT = SERVER_INCOMING_JITTER_BUFFER;

// It does not start playing before it has this amount of packages. The Jitter buffer is for bursts and lags from the server to the client
// 20 ms play-out jitter buffer
const int CLIENT_PLAYOUT_JITTER_BUFFER = 20 * SAMPLES_PER_MILLISECOND / SAMPLE_BUFFER_SIZE;
const int CLIENT_PLAYOUT_MAX_BUFFER = 60 * SAMPLES_PER_MILLISECOND / SAMPLE_BUFFER_SIZE;

// We need to define how many packages our ring buffer of sent packages has to be able to create FEC data
const int FEC_RINGBUFFER_SIZE = 16;

// By how much should we reduce the sample count for the FEC data?
const int FEC_SAMPLERATE_REDUCTION = 2;


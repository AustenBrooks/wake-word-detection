/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "audio_provider.h"
#include "ICM20948.h"
#include "ICM42622.h"
#include "arducam_mic.h"
#include "pico/stdlib.h"
#include <stdio.h>

extern "C" {
#include "pico/pdm_microphone.h"
}

#include "micro_features/micro_model_settings.h"

#define PDM_DEFAULT_PDM_BUFFER_SIZE 256
#define DEFAULT_PDM_BUFFER_SIZE 512
uint8_t DeviceWho = ICM20948_DEVICE;

namespace {
bool g_is_audio_initialized = false;
// An internal buffer able to fit 16x our sample size
constexpr int pdm_kAudioCaptureBufferSize = PDM_DEFAULT_PDM_BUFFER_SIZE * 16;
constexpr int kAudioCaptureBufferSize     = DEFAULT_PDM_BUFFER_SIZE * 16;
int16_t       pdm_g_audio_capture_buffer[pdm_kAudioCaptureBufferSize];
int16_t       g_audio_capture_buffer[kAudioCaptureBufferSize];
// A buffer that holds our output
int16_t g_audio_output_buffer[kMaxAudioSampleSize];
// Mark as volatile so we can check in a while loop to see if
// any samples have arrived yet.
volatile int32_t             g_latest_audio_timestamp = 0;
struct pdm_microphone_config pdm_config               = {
  .gpio_data          = 26,
  .gpio_clk           = 27,
  .pio                = pio0,
  .pio_sm             = 0,
  .sample_rate        = 16000,
  .sample_buffer_size = 256,
};
}  // namespace

void CaptureSamples() {
  if (DeviceWho == ICM42622_DEVICE_ID) {
    // This is how many bytes of new data we have each time this is called
    const int number_of_samples = PDM_DEFAULT_PDM_BUFFER_SIZE;
    // Calculate what timestamp the last audio sample represents
    const int32_t time_in_ms =
      g_latest_audio_timestamp + (number_of_samples / (kAudioSampleFrequency / 1000));
    // Determine the index, in the history of all samples, of the last sample
    const int32_t start_sample_offset =
      g_latest_audio_timestamp * (kAudioSampleFrequency / 1000);
    // Determine the index of this sample in our ring buffer
    const int capture_index = start_sample_offset % pdm_kAudioCaptureBufferSize;
    // Read the data to the correct place in our buffer
    pdm_microphone_read(pdm_g_audio_capture_buffer + capture_index,
                        PDM_DEFAULT_PDM_BUFFER_SIZE);
    // analog_microphone_read(pdm_g_audio_capture_buffer + capture_index,
    // PDM_DEFAULT_PDM_BUFFER_SIZE); This is how we let the outside world know that new
    // audio data has arrived.
    g_latest_audio_timestamp = time_in_ms;
  }
  else {
    // This is how many bytes of new data we have each time this is called
    // const int number_of_samples = DEFAULT_PDM_BUFFER_SIZE;
    // Calculate what timestamp the last audio sample represents
    const int32_t time_in_ms = g_latest_audio_timestamp + 64;
    // Determine the index, in the history of all samples, of the last sample
    const int32_t start_sample_offset = g_latest_audio_timestamp << 4;
    // Determine the index of this sample in our ring buffer
    const int capture_index = start_sample_offset % kAudioCaptureBufferSize;
    // config.data_buf = g_audio_capture_buffer + capture_index;
    // Read the data to the correct place in our buffer
    //  PDM.read(g_audio_capture_buffer + capture_index, DEFAULT_PDM_BUFFER_SIZE);
    // This is how we let the outside world know that new audio data has arrived.
    g_latest_audio_timestamp = time_in_ms;
  }
}

TfLiteStatus InitAudioRecording(tflite::ErrorReporter *error_reporter) {
  i2c_init(I2C_PORT, 400 * 1000);
  gpio_set_function(4, GPIO_FUNC_I2C);
  gpio_set_function(5, GPIO_FUNC_I2C);
  gpio_pull_up(4);
  gpio_pull_up(5);
  // sleep_ms(1000);
  uint8_t DeviceID = ICM42622::Icm42622CheckID();
  if (DeviceID == ICM42622_DEVICE_ID) {
    DeviceWho = ICM42622_DEVICE_ID;
    pdm_microphone_init(&pdm_config);
    pdm_microphone_set_samples_ready_handler(CaptureSamples);
    pdm_microphone_start();
    // Block until we have our first audio sample
    while (!g_latest_audio_timestamp) {}
  }
  else {
    DeviceWho       = ICM20948_DEVICE;
    config.update   = &CaptureSamples;
    config.data_buf = g_audio_capture_buffer;
    mic_i2s_init(&config);
    // Block until we have our first audio sample
    while (!g_latest_audio_timestamp) {}
  }
  return kTfLiteOk;
}

TfLiteStatus GetAudioSamples(tflite::ErrorReporter *error_reporter, int start_ms,
                             int duration_ms, int *audio_samples_size,
                             int16_t **audio_samples) {
  // Set everything up to start receiving audio
  if (!g_is_audio_initialized) {
    TfLiteStatus init_status = InitAudioRecording(error_reporter);
    if (init_status != kTfLiteOk) {
      return init_status;
    }
    g_is_audio_initialized = true;
  }
  // This next part should only be called when the main thread notices that the
  // latest audio sample data timestamp has changed, so that there's new data
  // in the capture ring buffer. The ring buffer will eventually wrap around and
  // overwrite the data, but the assumption is that the main thread is checking
  // often enough and the buffer is large enough that this call will be made
  // before that happens.
  if (DeviceWho == ICM42622_DEVICE_ID) {
    // Determine the index, in the history of all samples, of the first
    // sample we want
    const int start_offset = start_ms * (kAudioSampleFrequency / 1000);
    // Determine how many samples we want in total
    const int duration_sample_count = duration_ms * (kAudioSampleFrequency / 1000);
    for (int i = 0; i < duration_sample_count; ++i) {
      // For each sample, transform its index in the history of all samples into
      // its index in pdm_g_audio_capture_buffer
      const int capture_index = (start_offset + i) % pdm_kAudioCaptureBufferSize;
      // Write the sample to the output buffer
      g_audio_output_buffer[i] = pdm_g_audio_capture_buffer[capture_index];
    }
  }
  else {
    // Determine the index, in the history of all samples, of the first
    // sample we want
    const int start_offset = start_ms * (kAudioSampleFrequency / 1000);
    // Determine how many samples we want in total
    const int duration_sample_count = duration_ms * (kAudioSampleFrequency / 1000);

    static int sta = 0;
    sta            = (1 + sta) % 2;
    gpio_put(16, sta);

    //  printf("%d\n", duration_sample_count);
    //  printf("%d\n", start_offset);
    for (int i = 0; i < duration_sample_count; ++i) {
      // For each sample, transform its index in the history of all samples into
      // its index in g_audio_capture_buffer
      const int capture_index = (start_offset + i) % kAudioCaptureBufferSize;
      // Write the sample to the output buffer
      g_audio_output_buffer[i] = g_audio_capture_buffer[capture_index];
    }
  }

  // Set pointers to provide access to the audio
  *audio_samples_size = kMaxAudioSampleSize;
  *audio_samples      = g_audio_output_buffer;

  return kTfLiteOk;
}

int32_t LatestAudioTimestamp() {
  return g_latest_audio_timestamp;
}

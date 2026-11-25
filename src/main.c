#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Errors.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#define TTI_SAMPLES 1920
#define ADC_FULL_SCALE 2048
#define CLIP_LEVEL 2040
static volatile sig_atomic_t stop_flag = 0;
static void on_stop(int sig) { (void)sig; stop_flag = 1; }
int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 3) {
        printf("Usage: %s <tx|rx> <uri> [gain_tx 0..89] [gain_rx 0..73]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *mode = argv[1];
    const char *uri  = argv[2];
    int is_tx_mode = (strcmp(mode, "tx") == 0);
    int is_rx_mode = (strcmp(mode, "rx") == 0);
    if (!is_tx_mode && !is_rx_mode) {
        printf("Unknown mode '%s'. Use 'tx' or 'rx'.\n", mode);
        return EXIT_FAILURE;
    }
    if (!(strncmp(uri, "usb:", 4) == 0 || strncmp(uri, "ip:", 3) == 0)) {
        printf("Bad uri '%s'\n", uri);
        return EXIT_FAILURE;
    }
    double gain_tx = 40.0;
    double gain_rx = 30.0;
    if (argc >= 4) gain_tx = atof(argv[3]);
    if (argc >= 5) gain_rx = atof(argv[4]);
    if (gain_tx < 0.0) gain_tx = 0.0;
    if (gain_tx > 89.0) gain_tx = 89.0;
    if (gain_rx < 0.0) gain_rx = 0.0;
    if (gain_rx > 73.0) gain_rx = 73.0;
    double tx_freq, rx_freq;
    const char *out_file;
    const char *in_file = "tx_name.pcm";
    if (is_tx_mode) { tx_freq = 800e6; rx_freq = 840e6; out_file = "rx_from_840.pcm"; }
    else { tx_freq = 840e6; rx_freq = 800e6; out_file = "rx_from_800.pcm"; }
    printf("[MODE %s] TX=%.0f MHz | RX=%.0f MHz | gain_tx=%.1f gain_rx=%.1f\n",
           is_tx_mode ? "TX" : "RX", tx_freq / 1e6, rx_freq / 1e6, gain_tx, gain_rx);
    const double sample_rate = 1e6;
    const double bandwidth   = 1e6;
    SoapySDRStream *txStream = NULL;
    SoapySDRStream *rxStream = NULL;
    int16_t *tx_buffer   = NULL;
    int16_t *zero_buffer = NULL;
    int16_t *rx_buffer   = NULL;
    FILE *file_rx = NULL;
    int rc = EXIT_FAILURE;
    int tx_active = 0, rx_active = 0;
    size_t count = 0;
    size_t read_errors = 0, short_writes = 0;
    unsigned long long total_samples = 0, total_clipped = 0;
    int peak_all = 0;
    const long timeoutUs = 1000000;
    SoapySDRKwargs args;
    memset(&args, 0, sizeof(args));
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "loopback", "0");
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    if (sdr == NULL) {
        printf("Open fail (%s): %s\n", uri, SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }
    printf("[%s] Device opened.\n", uri);
    size_t channels[] = {0};
    if (is_tx_mode) {
        SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
        SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL);
        SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_TX, 0, bandwidth);
        SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, gain_tx);
        txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, 1, NULL);
        if (!txStream) { printf("setupStream TX fail: %s\n", SoapySDRDevice_lastError()); goto cleanup; }
    } else {
        SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, 0.0);
    }
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL);
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, bandwidth);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, gain_rx);
    rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, NULL);
    if (!rxStream) { printf("setupStream RX fail: %s\n", SoapySDRDevice_lastError()); goto cleanup; }
    printf("TTI: %d samples per read/write\n", TTI_SAMPLES);
    tx_buffer   = (int16_t *)calloc(2 * TTI_SAMPLES, sizeof(int16_t));
    zero_buffer = (int16_t *)calloc(2 * TTI_SAMPLES, sizeof(int16_t));
    rx_buffer   = (int16_t *)malloc(2 * TTI_SAMPLES * sizeof(int16_t));
    if (!tx_buffer || !zero_buffer || !rx_buffer) { printf("Malloc fail\n"); goto cleanup; }
    if (is_tx_mode) {
        FILE *f_in = fopen(in_file, "rb");
        if (!f_in) { printf("ERROR: Cannot open '%s'. Run tx_generator.py first!\n", in_file); goto cleanup; }
        fseek(f_in, 0, SEEK_END);
        long f_size = ftell(f_in);
        rewind(f_in);
        size_t n_int16 = (size_t)f_size / sizeof(int16_t);
        n_int16 -= n_int16 % 2;
        if (n_int16 == 0) { printf("ERROR: '%s' is empty\n", in_file); fclose(f_in); goto cleanup; }
        int16_t *file_data = (int16_t *)malloc(n_int16 * sizeof(int16_t));
        if (!file_data || fread(file_data, sizeof(int16_t), n_int16, f_in) != n_int16) {
            printf("ERROR: read '%s' failed\n", in_file); free(file_data); fclose(f_in); goto cleanup;
        }
        fclose(f_in);
        size_t cap = 2 * TTI_SAMPLES;
        if (n_int16 >= cap) {
            memcpy(tx_buffer, file_data, cap * sizeof(int16_t));
        } else {
            size_t copies = cap / n_int16;
            for (size_t c = 0; c < copies; c++) memcpy(&tx_buffer[c * n_int16], file_data, n_int16 * sizeof(int16_t));
        }
        free(file_data);
        printf("[TX] Loaded %zu complex samples from '%s' into %d-sample TTI buffer\n", n_int16 / 2, in_file, TTI_SAMPLES);
    }
    if (is_tx_mode) {
        if (SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0) != 0) { printf("Activate TX fail: %s\n", SoapySDRDevice_lastError()); goto cleanup; }
        tx_active = 1;
    }
    if (SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0) != 0) { printf("Activate RX fail: %s\n", SoapySDRDevice_lastError()); goto cleanup; }
    rx_active = 1;
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    file_rx = fopen(out_file, "wb");
    if (!file_rx) { printf("Cannot open %s\n", out_file); goto cleanup; }
    printf("Start Full-Duplex Loop... (Ctrl+C to stop). Output: %s\n", out_file);
    while (!stop_flag) {
        void *rx_buffs[] = { rx_buffer };
        int flags_rx = 0;
        long long timeNs_rx = 0;
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, TTI_SAMPLES, &flags_rx, &timeNs_rx, timeoutUs);
        if (sr > 0) {
            long long sum_power = 0;
            int peak = 0;
            int clipped = 0;
            for (int i = 0; i < sr; i++) {
                int I = rx_buffer[2 * i];
                int Q = rx_buffer[2 * i + 1];
                sum_power += (long long)I * I + (long long)Q * Q;
                int aI = abs(I), aQ = abs(Q);
                if (aI > peak) peak = aI;
                if (aQ > peak) peak = aQ;
                if (aI >= CLIP_LEVEL || aQ >= CLIP_LEVEL) clipped++;
            }
            double rms = sqrt((double)sum_power / (double)(2LL * sr));
            total_samples += (unsigned long long)sr;
            total_clipped += (unsigned long long)clipped;
            if (peak > peak_all) peak_all = peak;
            if (count % 125 == 0) {
                printf("Buf:%zu RMS:%.1f peak:%d/%d clip:%.1f%%\n", count, rms, peak, ADC_FULL_SCALE, 100.0 * clipped / sr);
            }
            fwrite(rx_buffer, sizeof(int16_t), (size_t)(2 * sr), file_rx);
            fflush(file_rx);
        } else if (sr != SOAPY_SDR_TIMEOUT) {
            read_errors++;
        }
        if (is_tx_mode) {
            const int16_t *src = (count % 2 == 0) ? tx_buffer : zero_buffer;
            size_t done = 0;
            while (done < TTI_SAMPLES && !stop_flag) {
                const void *buffs[] = { src + 2 * done };
                int flags_tx = 0;
                int w = SoapySDRDevice_writeStream(sdr, txStream, buffs, TTI_SAMPLES - done, &flags_tx, 0, 100000);
                if (w > 0) {
                    done += (size_t)w;
                } else {
                    break;
                }
            }
            if (done != TTI_SAMPLES) short_writes++;
        }
        count++;
    }
    printf("Done. Bufs: %zu | read errors: %zu | short TX writes: %zu\n", count, read_errors, short_writes);
    if (total_samples > 0) {
        printf("Overall: peak %d/%d, clipped samples %.2f%%\n", peak_all, ADC_FULL_SCALE, 100.0 * (double)total_clipped / (double)total_samples);
        if (peak_all >= CLIP_LEVEL) printf("WARNING: RX saturated. Lower gain_tx and/or gain_rx.\n");
    }
    rc = EXIT_SUCCESS;
cleanup:
    if (file_rx) fclose(file_rx);
    if (tx_active) SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    if (rx_active) SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    if (txStream) SoapySDRDevice_closeStream(sdr, txStream);
    if (rxStream) SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_unmake(sdr);
    free(tx_buffer);
    free(zero_buffer);
    free(rx_buffer);
    usleep(500000);
    return rc;
}
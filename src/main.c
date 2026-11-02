

#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t stop_flag = 0;
static void on_stop(int sig) { (void)sig; stop_flag = 1; }

static void usage(const char *prog)
{
    printf("Usage: %s <tx|rx> <uri> [tx_file]\n", prog);
    printf("  tx|rx    mode: tx передаёт файл и пишет rx_from_840.pcm;\n");
    printf("           rx только пишет rx_from_800.pcm\n");
    printf("  uri      адрес платы из `SoapySDRUtil --find` (usb:3.4.5, ip:192.168.2.1)\n");
    printf("  tx_file  CS16-файл для передачи (по умолчанию tx_name.pcm,\n");
    printf("           генерируется: python3 scripts/gen_tx_name.py --name TIMUR)\n");
    printf("Stop: Ctrl+C — потоки закрываются, .pcm сохраняется.\n");
}

int main(int argc, char *argv[])
{
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argv[1];
    const char *uri  = argv[2];
    const char *in_file = (argc > 3) ? argv[3] : "tx_name.pcm";

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

    double tx_freq, rx_freq;
    const char *out_file;
    if (is_tx_mode) {
        tx_freq = 800e6;
        rx_freq = 840e6;
        out_file = "rx_from_840.pcm";
        printf("[MODE TX] TX=%.0f MHz | RX=%.0f MHz | file=%s\n", tx_freq / 1e6, rx_freq / 1e6, in_file);
    } else {
        tx_freq = 840e6;
        rx_freq = 800e6;
        out_file = "rx_from_800.pcm";
        printf("[MODE RX] RX=%.0f MHz (TX выключен)\n", rx_freq / 1e6);
    }

    SoapySDRKwargs args = {};
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

    const double sample_rate = 1e6;
    const double bandwidth   = 200e3;  
    const double gain_tx     = 71.0;
    const double gain_rx     = 60.0;

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate) != 0 ||
        SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL) != 0 ||
        SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_TX, 0, bandwidth) != 0 ||
        SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, gain_tx) != 0) {
        printf("TX setup fail: %s\n", SoapySDRDevice_lastError());
    }

    size_t channels[] = {0};
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, 1, NULL);
    if (!txStream) {
        printf("setupStream TX fail\n");
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate) != 0 ||
        SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL) != 0 ||
        SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, bandwidth) != 0 ||
        SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, gain_rx) != 0) {
        printf("RX setup fail: %s\n", SoapySDRDevice_lastError());
    }

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, NULL);
    if (!rxStream) {
        printf("setupStream RX fail\n");
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    printf("MTU: TX=%lu, RX=%lu\n", (unsigned long)tx_mtu, (unsigned long)rx_mtu);

    int16_t *tx_buffer = (int16_t *)malloc(2 * tx_mtu * sizeof(int16_t));
    int16_t *rx_buffer = (int16_t *)malloc(2 * rx_mtu * sizeof(int16_t));
    if (!tx_buffer || !rx_buffer) {
        printf("Malloc fail\n");
        free(tx_buffer);
        free(rx_buffer);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    
    int16_t *file_data = NULL;
    size_t file_num_int16 = 0;
    if (is_tx_mode) {
        FILE *f_in = fopen(in_file, "rb");
        if (!f_in) {
            printf("ERROR: cannot open '%s'. Run: python3 scripts/gen_tx_name.py\n", in_file);
            free(tx_buffer); free(rx_buffer);
            SoapySDRDevice_closeStream(sdr, txStream);
            SoapySDRDevice_closeStream(sdr, rxStream);
            SoapySDRDevice_unmake(sdr);
            return EXIT_FAILURE;
        }
        fseek(f_in, 0, SEEK_END);
        long f_size = ftell(f_in);
        rewind(f_in);
        file_num_int16 = (size_t)f_size / sizeof(int16_t);
        file_data = (int16_t *)malloc(file_num_int16 * sizeof(int16_t));
        if (!file_data || fread(file_data, sizeof(int16_t), file_num_int16, f_in) != file_num_int16) {
            printf("ERROR: read '%s' fail\n", in_file);
            fclose(f_in);
            free(file_data); free(tx_buffer); free(rx_buffer);
            SoapySDRDevice_closeStream(sdr, txStream);
            SoapySDRDevice_closeStream(sdr, rxStream);
            SoapySDRDevice_unmake(sdr);
            return EXIT_FAILURE;
        }
        fclose(f_in);
        printf("[TX] Loaded %zu complex samples from '%s'\n", file_num_int16 / 2, in_file);
    }

    if (SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0) != 0) {
        printf("Activate TX fail\n");
        free(tx_buffer); free(rx_buffer); free(file_data);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    if (SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0) != 0) {
        printf("Activate RX fail\n");
        SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
        free(tx_buffer); free(rx_buffer); free(file_data);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);

    FILE *file_rx = fopen(out_file, "wb");
    if (!file_rx) {
        printf("Cannot open %s\n", out_file);
        SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
        SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
        free(tx_buffer); free(rx_buffer); free(file_data);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    printf("Start Full-Duplex Loop... (Ctrl+C to stop)\n");

    size_t count = 0;
    size_t tx_offset = 0;
    unsigned long tx_errors = 0;
    unsigned long rx_errors = 0;
    const long timeoutUs = 1000000;

    while (!stop_flag) {
        void *rx_buffs[] = { rx_buffer };
        int flags_rx = 0;
        long long timeNs_rx = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu,
                                           &flags_rx, &timeNs_rx, timeoutUs);
        if (sr >= 0) {
            long long sum_power = 0;
            for (int i = 0; i < sr; i++) {
                int16_t I = rx_buffer[2 * i];
                int16_t Q = rx_buffer[2 * i + 1];
                sum_power += (long long)I * I + (long long)Q * Q;
            }
            double rms = (sr > 0) ? sqrt((double)sum_power / (double)(2LL * sr)) : 0.0;
            if (count % 100 == 0) {
                printf("Buf:%lu RMS:%.2f\n", (unsigned long)count, rms);
            }
            fwrite(rx_buffer, sizeof(int16_t), (size_t)(2 * sr), file_rx);
        } else if (sr != SOAPY_SDR_TIMEOUT) {
            rx_errors++;
            if (rx_errors % 100 == 1) {
                printf("readStream error %d (%s), total %lu\n",
                       sr, SoapySDRDevice_lastError(), rx_errors);
            }
        }

        if (is_tx_mode && file_data) {
            
            size_t buf_samples = tx_mtu * 2;
            size_t pos = 0;
            while (pos < buf_samples) {
                size_t remaining_in_buf = buf_samples - pos;
                size_t remaining_in_file = file_num_int16 - tx_offset;
                size_t to_copy = (remaining_in_buf < remaining_in_file)
                                 ? remaining_in_buf : remaining_in_file;
                memcpy(&tx_buffer[pos], &file_data[tx_offset], to_copy * sizeof(int16_t));
                pos += to_copy;
                tx_offset += to_copy;
                if (tx_offset >= file_num_int16) {
                    tx_offset = 0;
                }
            }

            void *tx_buffs[] = { tx_buffer };
            int flags_tx = 0;
            int st = SoapySDRDevice_writeStream(sdr, txStream,
                                                (const void * const *)tx_buffs,
                                                tx_mtu, &flags_tx, 0, 100000);
            if (st < 0 && st != SOAPY_SDR_OVERFLOW) {
                tx_errors++;
                if (tx_errors % 100 == 1) {
                    printf("writeStream error %d (%s), total %lu\n",
                           st, SoapySDRDevice_lastError(), tx_errors);
                }
            }
        }

        count++;
    }

    fclose(file_rx);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, txStream);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_unmake(sdr);
    free(tx_buffer);
    free(rx_buffer);
    free(file_data);

    usleep(500000);

    printf("Done. Buffers processed: %lu, RX errors: %lu, TX errors: %lu\n",
           (unsigned long)count, rx_errors, tx_errors);
    printf("Saved: %s\n", out_file);
    printf("Next: python3 scripts/rx_offline.py %s decode\n", out_file);
    return EXIT_SUCCESS;
}

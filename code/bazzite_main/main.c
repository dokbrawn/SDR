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

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <tx|rx> <uri>\n", argv[0]);
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

    double tx_freq, rx_freq;
    const char *out_file;

    if (is_tx_mode) {
        tx_freq = 800e6;
        rx_freq = 840e6;
        out_file = "rx_from_840.pcm"; 
        printf("[MODE TX] Device will TX on %.0f MHz and RX on %.0f MHz.\n", tx_freq/1e6, rx_freq/1e6);
    } else {
        tx_freq = 840e6;
        rx_freq = 800e6;
        out_file = "rx_from_800.pcm";
        printf("[MODE RX] Device will TX on %.0f MHz and RX on %.0f MHz.\n", tx_freq/1e6, rx_freq/1e6);
    }

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "loopback", "0");
    
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
    const double tone_hz     = 10000.0; 
    const int16_t amp        = 32000;    

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL);
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_TX, 0, bandwidth);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, gain_tx);

    size_t channels[] = {0};
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(
        sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, 1, NULL);
    
    if (!txStream) {
        printf("setupStream TX fail\n");
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL);
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, bandwidth);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, gain_rx);

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

    for (size_t i = 0; i < tx_mtu; i++) {
        double t_sec = (double)i / sample_rate;
        double phase = 2.0 * M_PI * tone_hz * t_sec;
        tx_buffer[2 * i]     = (int16_t)(amp * sin(phase));
        tx_buffer[2 * i + 1] = (int16_t)(amp * cos(phase));
    }

    if (SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0) != 0) {
        printf("Activate TX fail\n");
        free(tx_buffer); free(rx_buffer);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }
    
    if (SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0) != 0) {
        printf("Activate RX fail\n");
        SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
        free(tx_buffer); free(rx_buffer);
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
        free(tx_buffer); free(rx_buffer);
        SoapySDRDevice_closeStream(sdr, txStream);
        SoapySDRDevice_closeStream(sdr, rxStream);
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    printf("Start Stable Full-Duplex Loop... (Ctrl+C to stop)\n");
    
    size_t count = 0;
    const long timeoutUs = 1000000; 

    while (!stop_flag) {
        void *rx_buffs[] = { rx_buffer };
        int flags_rx = 0;
        long long timeNs_rx = 0;
        
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags_rx, &timeNs_rx, timeoutUs);
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
        }

        void *tx_buffs[] = { tx_buffer };
        int flags_tx = 0;
        int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const *)tx_buffs, tx_mtu, &flags_tx, 0, 100000);
        if (st < 0 && st != SOAPY_SDR_OVERFLOW) {
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
    
    usleep(500000); 
    
    printf("Done. Total bufs processed: %lu\n", (unsigned long)count);
    return EXIT_SUCCESS;
}

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
    double tx_freq, rx_freq;
    const char *out_file;

    if (is_tx_mode) {
        tx_freq = 800e6; rx_freq = 840e6; out_file = "rx_from_840.pcm";
        printf("[TX] TX=%.0f MHz | RX=%.0f MHz\n", tx_freq/1e6, rx_freq/1e6);
    } else {
        tx_freq = 840e6; rx_freq = 800e6; out_file = "rx_from_800.pcm";
        printf("[RX] TX=%.0f MHz | RX=%.0f MHz\n", tx_freq/1e6, rx_freq/1e6);
    }

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "loopback", "0");
    
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    if (!sdr) { printf("Open fail: %s\n", SoapySDRDevice_lastError()); return 1; }

    const double sr = 1e6;
    size_t ch[] = {0};
    
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sr);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL);
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_TX, 0, 200e3);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, 71.0);
    SoapySDRStream *txS = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, ch, 1, NULL);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txS);

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sr);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL);
    SoapySDRDevice_setBandwidth(sdr, SOAPY_SDR_RX, 0, 200e3);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, 60.0);
    SoapySDRStream *rxS = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, ch, 1, NULL);
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxS);

    int16_t *tx_buf = (int16_t*)malloc(2*tx_mtu*sizeof(int16_t));
    int16_t *rx_buf = (int16_t*)malloc(2*rx_mtu*sizeof(int16_t));

    const int16_t dc_val = 8000; 
    for(size_t i=0; i<tx_mtu; i++) { tx_buf[2*i]=dc_val; tx_buf[2*i+1]=dc_val; }

    SoapySDRDevice_activateStream(sdr, txS, 0,0,0);
    SoapySDRDevice_activateStream(sdr, rxS, 0,0,0);
    signal(SIGINT, on_stop);

    FILE *f = fopen(out_file, "wb");
    printf("Running... Ctrl+C to stop\n");
    
    size_t cnt=0;
    while(!stop_flag) {
        void *rxb[]={rx_buf}; int fl; long long tns;
        int sr_read = SoapySDRDevice_readStream(sdr, rxS, rxb, rx_mtu, &fl, &tns, 1000000);
        if(sr_read > 0) {
            fwrite(rx_buf, sizeof(int16_t), 2*sr_read, f);
            if(cnt%200==0) {
                long long p=0; 
                for(int i=0;i<sr_read;i++) p+=(long long)rx_buf[2*i]*rx_buf[2*i]+(long long)rx_buf[2*i+1]*rx_buf[2*i+1];
                printf("Buf:%lu RMS:%.1f\n", (unsigned long)cnt, sqrt((double)p/(2.0*sr_read)));
            }
        }
        
        void *txb[]={tx_buf}; int flt;
        SoapySDRDevice_writeStream(sdr, txS, (const void*const*)txb, tx_mtu, &flt, 0, 100000);
        cnt++;
    }

    fclose(f);
    SoapySDRDevice_deactivateStream(sdr, txS, 0,0);
    SoapySDRDevice_deactivateStream(sdr, rxS, 0,0);
    SoapySDRDevice_closeStream(sdr, txS);
    SoapySDRDevice_closeStream(sdr, rxS);
    SoapySDRDevice_unmake(sdr);
    free(tx_buf); free(rx_buf);
    usleep(500000);
    printf("Done.\n");
    return 0;
}

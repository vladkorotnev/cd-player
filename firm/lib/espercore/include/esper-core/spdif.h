#pragma once
#include <esper-core/thread_safe_i2c.h>

namespace Platform {
    class SPDIFTransceiver {
    public:
        virtual void initialize() {}

        /// @brief When enabled, the transceiver must receive the input data from the CD drive and pass it on to the I2S bus.
        ///        When disabled, the transceiver must release the bus completely to allow another device to push sound data through. 
        virtual void set_enabled(bool) {}

        virtual bool need_deemphasis() { return false; }
        virtual bool locked_on() { return false; }
    };

    class WM8805: public SPDIFTransceiver {
    public: 
        WM8805(Core::ThreadSafeI2C * bus, uint8_t address):
            i2c(bus),
            addr(address)
        {}

        void initialize() override;
        void set_enabled(bool) override;
        bool need_deemphasis() override;
        bool locked_on() override;
        
    private:
        Core::ThreadSafeI2C * i2c;
        uint8_t addr;
        bool inited = false;

        void write(uint8_t regi, uint8_t val);
        uint8_t read(uint8_t regi);

        enum Register: uint8_t {
            RST_DEVID1 = 0x0,
            DEVID2,
            DEVREV,
            PLL1,
            PLL2,
            PLL3,
            PLL4,
            PLL5,
            PLL6,
            SPDMODE,
            INTMASK,
            INTSTAT,
            SPDSTAT,
            RXCHAN1,
            RXCHAN2,
            RXCHAN3,
            RXCHAN4,
            RXCHAN5,
            SPDTX1,
            SPDTX2,
            SPDTX3,
            SPDTX4,
            SPDTX5,
            GPO01,
            GPO23,
            GPO45,
            GPO67,
            AIFTX,
            AIFRX,
            SPDRX1,
            PWRDN
        };

        union PLL5Reg {
            enum ClkDiv: uint8_t {
                CLKDIV_512FS = 0,
                CLKDIV_256FS,
                CLKDIV_128FS,
                CLKDIV_64FS
            };
            struct {
                uint8_t freq_mode_ro: 2;
                bool frac_en: 1;
                bool mclkdiv: 1;
                ClkDiv clkout_div: 2;
            };
            uint8_t value;
        };

        union PLL6Reg {
            struct {
                uint8_t rx_in_select: 3;
                uint8_t clkout_src: 1;
                bool clkout_dis: 1;
                bool fill_mode: 1;
                bool always_valid: 1;
                uint8_t mclksrc: 1;
            };
            uint8_t value;
        };

        union AIFRXReg {
            enum AIFRXFmt: uint8_t {
                RIGHT_JUSTIFY = 0,
                LEFT_JUSTIFY = 1,
                I2S_FORMAT = 2,
                DSP_FORMAT = 3
            };

            enum AIFRXWordLen: uint8_t {
                WLEN_16BIT = 0,
                WLEN_24BIT = 0b10
            };
            struct {
                AIFRXFmt format: 2;
                AIFRXWordLen word_len: 2;
                bool bclk_invert: 1;
                bool lrck_polarity: 1;
                bool master: 1;
                bool sync: 1;
            };
            uint8_t value;
        };

        union PWRDNReg {
            struct {
                bool pll: 1;
                bool spdif_rx: 1;
                bool spdif_tx: 1;
                bool osc: 1;
                bool aif: 1;
                bool triop: 1;
            };
            uint8_t value;
        };

        union SPDSTATReg {
            enum Frequency: uint8_t {
                FS_192K = 0,
                FS_96K_OR_88K2 = 1, // <- how is a device with such good PLL that uncertain?
                FS_48K_OR_44K1 = 2,
                FS_32K = 3
            };
            struct {
                bool no_audio: 1;
                bool no_pcm: 1;
                bool no_copyright: 1;
                bool deemphasis: 1;
                Frequency frequency: 2;
                bool unlock: 1;
            };
            uint8_t value;
        };

        TickType_t last_sts_refresh = 0;
        SPDSTATReg sts = { 0 };
        SPDSTATReg get_fresh_status();
    };
};
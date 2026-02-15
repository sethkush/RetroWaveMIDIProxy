// Build-time tool: exports all libADLMIDI embedded banks as .wopl files.
// Usage: export_banks <output_directory>

#include <adlmidi.h>
#include <wopl_file.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static void adl_to_wopl_operator(const ADL_Operator &src, WOPLOperator &dst)
{
    dst.avekf_20   = src.avekf_20;
    dst.ksl_l_40   = src.ksl_l_40;
    dst.atdec_60   = src.atdec_60;
    dst.susrel_80  = src.susrel_80;
    dst.waveform_E0 = src.waveform_E0;
}

static void adl_to_wopl_instrument(const ADL_Instrument &src, WOPLInstrument &dst)
{
    memset(&dst, 0, sizeof(dst));
    dst.note_offset1        = src.note_offset1;
    dst.note_offset2        = src.note_offset2;
    dst.midi_velocity_offset = src.midi_velocity_offset;
    dst.second_voice_detune = src.second_voice_detune;
    dst.percussion_key_number = src.percussion_key_number;
    dst.inst_flags          = src.inst_flags;
    dst.fb_conn1_C0         = src.fb_conn1_C0;
    dst.fb_conn2_C0         = src.fb_conn2_C0;
    for (int i = 0; i < 4; ++i)
        adl_to_wopl_operator(src.operators[i], dst.operators[i]);
    dst.delay_on_ms  = src.delay_on_ms;
    dst.delay_off_ms = src.delay_off_ms;
}

// Sanitize a bank name for use as a filename
static std::string sanitize_filename(const char *name)
{
    std::string s(name);
    for (char &c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ';')
            c = ',';
    }
    // Trim trailing spaces
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output_directory>\n", argv[0]);
        return 1;
    }

    const char *outdir = argv[1];
    int count = adl_getBanksCount();
    const char *const *names = adl_getBankNames();

    fprintf(stderr, "Exporting %d banks to %s\n", count, outdir);

    for (int b = 0; b < count; ++b) {
        ADL_MIDIPlayer *player = adl_init(44100);
        if (!player) {
            fprintf(stderr, "Failed to init libADLMIDI for bank %d\n", b);
            continue;
        }

        if (adl_setBank(player, b) < 0) {
            fprintf(stderr, "Failed to set bank %d: %s\n", b, adl_errorInfo(player));
            adl_close(player);
            continue;
        }

        // Count melodic and percussion banks
        int mel_count = 0, perc_count = 0;
        {
            ADL_Bank bank;
            if (adl_getFirstBank(player, &bank) == 0) {
                do {
                    ADL_BankId id;
                    adl_getBankId(player, &bank, &id);
                    if (id.percussive)
                        perc_count++;
                    else
                        mel_count++;
                } while (adl_getNextBank(player, &bank) == 0);
            }
        }

        if (mel_count == 0 && perc_count == 0) {
            adl_close(player);
            continue;
        }

        WOPLFile *wopl = WOPL_Init(
            static_cast<uint16_t>(std::max(mel_count, 1)),
            static_cast<uint16_t>(std::max(perc_count, 1))
        );
        if (!wopl) {
            adl_close(player);
            continue;
        }

        // Iterate banks and extract instruments
        int mel_idx = 0, perc_idx = 0;
        {
            ADL_Bank bank;
            if (adl_getFirstBank(player, &bank) == 0) {
                do {
                    ADL_BankId id;
                    adl_getBankId(player, &bank, &id);

                    WOPLBank *dst_bank;
                    if (id.percussive) {
                        if (perc_idx >= perc_count) continue;
                        dst_bank = &wopl->banks_percussive[perc_idx++];
                    } else {
                        if (mel_idx >= mel_count) continue;
                        dst_bank = &wopl->banks_melodic[mel_idx++];
                    }

                    dst_bank->bank_midi_msb = id.msb;
                    dst_bank->bank_midi_lsb = id.lsb;
                    snprintf(dst_bank->bank_name, sizeof(dst_bank->bank_name),
                             "%s", names[b]);

                    for (int i = 0; i < 128; ++i) {
                        ADL_Instrument adl_ins;
                        if (adl_getInstrument(player, &bank, static_cast<unsigned>(i), &adl_ins) == 0)
                            adl_to_wopl_instrument(adl_ins, dst_bank->ins[i]);
                    }
                } while (adl_getNextBank(player, &bank) == 0);
            }
        }

        wopl->banks_count_melodic = static_cast<uint16_t>(mel_idx > 0 ? mel_idx : 1);
        wopl->banks_count_percussion = static_cast<uint16_t>(perc_idx > 0 ? perc_idx : 1);

        // Write the .wopl file
        size_t size = WOPL_CalculateBankFileSize(wopl, 3);
        std::vector<uint8_t> buf(size);
        if (WOPL_SaveBankToMem(wopl, buf.data(), size, 3, 0) == 0) {
            std::string fname = sanitize_filename(names[b]);
            std::string path = std::string(outdir) + "/" +
                               std::to_string(b) + "-" + fname + ".wopl";
            FILE *fp = fopen(path.c_str(), "wb");
            if (fp) {
                fwrite(buf.data(), 1, size, fp);
                fclose(fp);
            } else {
                fprintf(stderr, "Failed to write %s\n", path.c_str());
            }
        }

        WOPL_Free(wopl);
        adl_close(player);
    }

    fprintf(stderr, "Done.\n");
    return 0;
}

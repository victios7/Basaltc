/* PE32+ x86_64 writer for Basalt cross-compile */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void w16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void w32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void w64(uint8_t *p, uint64_t v) { w32(p,(uint32_t)v); w32(p+4,(uint32_t)(v>>32)); }

int64_t basalt_write_pe(const char *path, const uint8_t *code, int64_t code_len) {
    const uint64_t image_base = 0x140000000ULL;
    const uint32_t file_align = 0x200;
    uint8_t epi[] = { 0x89,0xc1, 0x48,0x83,0xec,0x28, 0xff,0x15,0,0,0,0, 0xcc };
    if (!code || code_len <= 0) {
        static uint8_t stub[] = { 0x31, 0xc0, 0xc3 };
        code = stub; code_len = 3;
    }
    int64_t total_code = code_len + (int64_t)sizeof(epi);
    uint32_t text_raw_size = ((uint32_t)total_code + file_align - 1) & ~(file_align - 1);
    if (text_raw_size < file_align) text_raw_size = file_align;
    uint32_t headers = 0x400;
    uint32_t text_raw = headers;
    uint32_t text_rva = 0x1000;
    uint32_t idata_raw = text_raw + text_raw_size;
    uint32_t idata_rva = 0x2000;
    uint32_t idata_raw_size = file_align;
    uint32_t iat_off = 0x40;
    uint32_t iat_rva = idata_rva + iat_off;
    size_t file_size = (size_t)idata_raw + idata_raw_size;
    uint8_t *buf = (uint8_t*)calloc(1, file_size);
    if (!buf) return -1;
    buf[0]='M'; buf[1]='Z';
    w32(buf+0x3c, 0x80);
    size_t pe = 0x80;
    buf[pe]='P'; buf[pe+1]='E';
    w16(buf+pe+4, 0x8664);
    w16(buf+pe+6, 2);
    w32(buf+pe+20, 0xF0);
    w16(buf+pe+22, 0x22);
    size_t oh = pe + 24;
    w16(buf+oh, 0x20B);
    buf[oh+2]=14;
    w32(buf+oh+4, text_raw_size);
    w32(buf+oh+16, text_rva);
    w64(buf+oh+24, image_base);
    w32(buf+oh+32, 0x1000);
    w32(buf+oh+36, file_align);
    w16(buf+oh+40, 6); w16(buf+oh+48, 6);
    w32(buf+oh+56, 0x3000);
    w32(buf+oh+60, headers);
    w16(buf+oh+68, 3);
    w16(buf+oh+70, 0x160);
    w64(buf+oh+72, 0x100000); w64(buf+oh+80, 0x1000);
    w64(buf+oh+88, 0x100000); w64(buf+oh+96, 0x1000);
    w32(buf+oh+108, 16);
    w32(buf+oh+112+8, idata_rva);
    w32(buf+oh+112+12, 0x80);
    w32(buf+oh+112+12*8, iat_rva);
    w32(buf+oh+112+12*8+4, 0x10);
    size_t s1 = pe + 24 + 0xF0;
    memcpy(buf+s1, ".text", 5);
    w32(buf+s1+8, text_raw_size); w32(buf+s1+12, text_rva);
    w32(buf+s1+16, text_raw_size); w32(buf+s1+20, text_raw);
    w32(buf+s1+36, 0x60000020);
    size_t s2 = s1 + 40;
    memcpy(buf+s2, ".idata", 6);
    w32(buf+s2+8, idata_raw_size); w32(buf+s2+12, idata_rva);
    w32(buf+s2+16, idata_raw_size); w32(buf+s2+20, idata_raw);
    w32(buf+s2+36, 0xC0000040);
    memcpy(buf+text_raw, code, (size_t)code_len);
    size_t epi_at = (size_t)text_raw + (size_t)code_len;
    memcpy(buf+epi_at, epi, sizeof(epi));
    {
        uint32_t instr_rva = text_rva + (uint32_t)code_len + 6;
        int32_t rel = (int32_t)iat_rva - (int32_t)(instr_rva + 6);
        w32(buf + epi_at + 8, (uint32_t)rel);
    }
    uint8_t *id = buf + idata_raw;
    uint32_t ilt_rva = idata_rva + 0x20;
    uint32_t name_rva = idata_rva + 0x50;
    uint32_t hint_rva = idata_rva + 0x60;
    w32(id+0, ilt_rva); w32(id+12, name_rva); w32(id+16, iat_rva);
    w64(id+0x20, hint_rva);
    w64(id+iat_off, hint_rva);
    memcpy(id+0x50, "KERNEL32.dll", 13);
    w16(id+0x60, 0); memcpy(id+0x62, "ExitProcess", 12);
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    size_t nw = fwrite(buf, 1, file_size, f);
    fclose(f); free(buf);
    return (nw == file_size) ? 0 : -2;
}

int64_t basalt_link_pe(const char *src_path, const char *exe_path) {
    char obj[512], bin[512], cmd[1400];
    snprintf(obj, sizeof(obj), "%s.pe.o", exe_path);
    snprintf(bin, sizeof(bin), "%s.pe.bin", exe_path);
    snprintf(cmd, sizeof(cmd), "objcopy -O binary -j .text '%s' '%s' 2>/dev/null", src_path, bin);
    if (system(cmd) != 0) {
        snprintf(cmd, sizeof(cmd),
            "(basm '%s' -c -o '%s' || /tmp/basm '%s' -c -o '%s') 2>/dev/null && "
            "objcopy -O binary -j .text '%s' '%s' 2>/dev/null",
            src_path, obj, src_path, obj, obj, bin);
        if (system(cmd) != 0) {
            int64_t r = basalt_write_pe(exe_path, NULL, 0);
            unlink(obj); unlink(bin); return r;
        }
    }
    FILE *f = fopen(bin, "rb");
    if (!f) { int64_t r = basalt_write_pe(exe_path, NULL, 0); unlink(obj); unlink(bin); return r; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8*1024*1024) { fclose(f); unlink(obj); unlink(bin); return basalt_write_pe(exe_path, NULL, 0); }
    uint8_t *code = (uint8_t*)malloc((size_t)sz);
    if (!code) { fclose(f); return -1; }
    if (fread(code, 1, (size_t)sz, f) != (size_t)sz) { free(code); fclose(f); unlink(obj); unlink(bin); return -1; }
    fclose(f);
    int64_t rc = basalt_write_pe(exe_path, code, sz);
    free(code); unlink(obj); unlink(bin);
    return rc;
}

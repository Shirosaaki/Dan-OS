#include "kvm_stub.h"
#include "kmalloc.h"
#include "tty.h"
#include "string.h"
#include "keyboard.h"

static struct kvm_run *current_run = 0;
extern vm_t *current_vm;

#define FLAG_CF (1 << 0)
#define FLAG_PF (1 << 2)
#define FLAG_AF (1 << 4)
#define FLAG_ZF (1 << 6)
#define FLAG_SF (1 << 7)
#define FLAG_TF (1 << 8)
#define FLAG_IF (1 << 9)
#define FLAG_DF (1 << 10)
#define FLAG_OF (1 << 11)

int danos_kvm_create_vm(void) { return 100; }
int danos_kvm_create_vcpu(int vm_fd) {
    (void)vm_fd;
    current_run = (struct kvm_run*)kmalloc(sizeof(struct kvm_run));
    memset_k(current_run, 0, sizeof(struct kvm_run));
    return 101;
}
int danos_kvm_get_vcpu_mmap_size(void) { return sizeof(struct kvm_run); }
struct kvm_run* danos_kvm_map_run(int vcpu_fd) { (void)vcpu_fd; return current_run; }
int danos_kvm_set_user_memory_region(int vm_fd, struct kvm_userspace_memory_region *region) { (void)vm_fd; (void)region; return 0; }
int danos_kvm_get_sregs(int vcpu_fd, struct kvm_sregs *sregs) { (void)vcpu_fd; memset_k(sregs, 0, sizeof(struct kvm_sregs)); return 0; }
int danos_kvm_set_sregs(int vcpu_fd, struct kvm_sregs *sregs) { (void)vcpu_fd; (void)sregs; return 0; }
int danos_kvm_set_regs(int vcpu_fd, struct kvm_regs *regs) { (void)vcpu_fd; (void)regs; return 0; }

static inline uint32_t seg_addr(uint16_t seg, uint16_t off) { return ((uint32_t)seg << 4) + off; }
static uint8_t mem_read8(vm_t *vm, uint16_t seg, uint16_t off) {
    uint32_t addr = seg_addr(seg, off);
    if (addr >= vm->ram_size) return 0;
    return vm->ram[addr];
}
static uint16_t mem_read16(vm_t *vm, uint16_t seg, uint16_t off) {
    uint32_t addr = seg_addr(seg, off);
    if (addr + 1 >= vm->ram_size) return 0;
    return vm->ram[addr] | (vm->ram[addr + 1] << 8);
}
static void mem_write8(vm_t *vm, uint16_t seg, uint16_t off, uint8_t val) {
    uint32_t addr = seg_addr(seg, off);
    if (addr < vm->ram_size) vm->ram[addr] = val;
}
static void mem_write16(vm_t *vm, uint16_t seg, uint16_t off, uint16_t val) {
    uint32_t addr = seg_addr(seg, off);
    if (addr + 1 < vm->ram_size) { vm->ram[addr] = val & 0xFF; vm->ram[addr + 1] = (val >> 8) & 0xFF; }
}
static uint8_t fetch8(vm_t *vm) { return mem_read8(vm, vm->cpu.cs, vm->cpu.ip++); }
static uint16_t fetch16(vm_t *vm) { uint16_t v = mem_read16(vm, vm->cpu.cs, vm->cpu.ip); vm->cpu.ip += 2; return v; }
static void push16(vm_t *vm, uint16_t val) { vm->cpu.sp -= 2; mem_write16(vm, vm->cpu.ss, vm->cpu.sp, val); }
static uint16_t pop16(vm_t *vm) { uint16_t v = mem_read16(vm, vm->cpu.ss, vm->cpu.sp); vm->cpu.sp += 2; return v; }

static void set_flag(vm_t *vm, uint16_t flag, int cond) { if (cond) vm->cpu.flags |= flag; else vm->cpu.flags &= ~flag; }
static void update_logic_flags(vm_t *vm, uint16_t res, int size) {
    vm->cpu.flags &= ~(FLAG_CF | FLAG_OF | FLAG_SF | FLAG_ZF | FLAG_PF);
    uint8_t v = res & 0xFF; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    if (!(v & 1)) vm->cpu.flags |= FLAG_PF;
    if (size == 8) { if ((res & 0xFF) == 0) vm->cpu.flags |= FLAG_ZF; if (res & 0x80) vm->cpu.flags |= FLAG_SF; }
    else { if (res == 0) vm->cpu.flags |= FLAG_ZF; if (res & 0x8000) vm->cpu.flags |= FLAG_SF; }
}
static void update_arith_flags(vm_t *vm, uint32_t res, uint32_t src, uint32_t dst, int is_sub, int size) {
    update_logic_flags(vm, res, size);
    int cf = (size == 8) ? (res > 0xFF) : (res > 0xFFFF);
    if (is_sub) cf = (src > dst);
    set_flag(vm, FLAG_CF, cf);
    uint32_t sm = (size == 8) ? 0x80 : 0x8000;
    int s1 = (dst & sm) != 0, s2 = (src & sm) != 0, sr = (res & sm) != 0;
    set_flag(vm, FLAG_OF, is_sub ? ((s1 != s2) && (s1 != sr)) : ((s1 == s2) && (s1 != sr)));
}

typedef struct { uint8_t mod, reg, rm; uint16_t offset, seg; int is_mem; } modrm_t;
static void* get_reg_ptr(vm_t *vm, int reg, int w) {
    switch (reg) {
        case 0: return &vm->cpu.ax; case 1: return &vm->cpu.cx; case 2: return &vm->cpu.dx; case 3: return &vm->cpu.bx;
        case 4: return w ? (void*)&vm->cpu.sp : (void*)((uint8_t*)&vm->cpu.ax + 1);
        case 5: return w ? (void*)&vm->cpu.bp : (void*)((uint8_t*)&vm->cpu.cx + 1);
        case 6: return w ? (void*)&vm->cpu.si : (void*)((uint8_t*)&vm->cpu.dx + 1);
        case 7: return w ? (void*)&vm->cpu.di : (void*)((uint8_t*)&vm->cpu.bx + 1);
    } return 0;
}
static uint16_t* get_seg_ptr(vm_t *vm, int reg) {
    switch(reg & 3) { case 0: return &vm->cpu.es; case 1: return &vm->cpu.cs; case 2: return &vm->cpu.ss; case 3: return &vm->cpu.ds; } return 0;
}
static void decode_modrm(vm_t *vm, modrm_t *out) {
    uint8_t b = fetch8(vm);
    out->mod = (b >> 6) & 3; out->reg = (b >> 3) & 7; out->rm = b & 7; out->is_mem = (out->mod != 3); out->seg = vm->cpu.ds;
    if (!out->is_mem) return;
    int16_t disp = 0;
    if (out->mod == 1) disp = (int8_t)fetch8(vm); else if (out->mod == 2) disp = (int16_t)fetch16(vm);
    switch (out->rm) {
        case 0: out->offset = vm->cpu.bx + vm->cpu.si; break; case 1: out->offset = vm->cpu.bx + vm->cpu.di; break;
        case 2: out->offset = vm->cpu.bp + vm->cpu.si; out->seg = vm->cpu.ss; break;
        case 3: out->offset = vm->cpu.bp + vm->cpu.di; out->seg = vm->cpu.ss; break;
        case 4: out->offset = vm->cpu.si; break; case 5: out->offset = vm->cpu.di; break;
        case 6: if (out->mod == 0) out->offset = fetch16(vm); else { out->offset = vm->cpu.bp; out->seg = vm->cpu.ss; } break;
        case 7: out->offset = vm->cpu.bx; break;
    } out->offset += disp;
}
static uint16_t rm_read(vm_t *vm, modrm_t *m, int w) {
    if (!m->is_mem) { void *p = get_reg_ptr(vm, m->rm, w); return w ? *(uint16_t*)p : *(uint8_t*)p; }
    return w ? mem_read16(vm, m->seg, m->offset) : mem_read8(vm, m->seg, m->offset);
}
static void rm_write(vm_t *vm, modrm_t *m, int w, uint16_t val) {
    if (!m->is_mem) { void *p = get_reg_ptr(vm, m->rm, w); if(w) *(uint16_t*)p = val; else *(uint8_t*)p = val; }
    else { if(w) mem_write16(vm, m->seg, m->offset, val); else mem_write8(vm, m->seg, m->offset, val); }
}

static int emulate_instruction(vm_t *vm) {
    uint8_t opcode = fetch8(vm);
    modrm_t m; uint16_t src, dst, res; int w;

    switch (opcode) {
        case 0x88: case 0x89: w=opcode&1; decode_modrm(vm, &m); src=w?*(uint16_t*)get_reg_ptr(vm, m.reg, 1):*(uint8_t*)get_reg_ptr(vm, m.reg, 0); rm_write(vm, &m, w, src); break;
        case 0x8A: case 0x8B: w=opcode&1; decode_modrm(vm, &m); src=rm_read(vm, &m, w); if(w)*(uint16_t*)get_reg_ptr(vm, m.reg, 1)=src; else *(uint8_t*)get_reg_ptr(vm, m.reg, 0)=src; break;
        case 0xB0 ... 0xB7: *(uint8_t*)get_reg_ptr(vm, opcode&7, 0)=fetch8(vm); break;
        case 0xB8 ... 0xBF: *(uint16_t*)get_reg_ptr(vm, opcode&7, 1)=fetch16(vm); break;
        case 0x8E: decode_modrm(vm, &m); *get_seg_ptr(vm, m.reg)=rm_read(vm, &m, 1); break;
        case 0x50 ... 0x57: push16(vm, *(uint16_t*)get_reg_ptr(vm, opcode&7, 1)); break;
        case 0x58 ... 0x5F: *(uint16_t*)get_reg_ptr(vm, opcode&7, 1)=pop16(vm); break;
        case 0x84: case 0x85: w=opcode&1; decode_modrm(vm, &m); src=w?*(uint16_t*)get_reg_ptr(vm, m.reg, 1):*(uint8_t*)get_reg_ptr(vm, m.reg, 0); dst=rm_read(vm, &m, w); update_logic_flags(vm, dst&src, w?16:8); break;
        case 0x00 ... 0x03: case 0x08 ... 0x0B: case 0x20 ... 0x23: case 0x28 ... 0x2B: case 0x30 ... 0x33: case 0x38 ... 0x3B:
        {
            w=opcode&1; int dir=opcode&2; decode_modrm(vm, &m);
            uint16_t vr=w?*(uint16_t*)get_reg_ptr(vm, m.reg, 1):*(uint8_t*)get_reg_ptr(vm, m.reg, 0);
            uint16_t vm_val=rm_read(vm, &m, w);
            if(dir){ dst=vr; src=vm_val; } else { dst=vm_val; src=vr; }
            int op=(opcode>>3)&7;
            switch(op){
                case 0: res=dst+src; update_arith_flags(vm, res, src, dst, 0, w?16:8); break;
                case 1: res=dst|src; update_logic_flags(vm, res, w?16:8); break;
                case 4: res=dst&src; update_logic_flags(vm, res, w?16:8); break;
                case 5: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
                case 6: res=dst^src; update_logic_flags(vm, res, w?16:8); break;
                case 7: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
            }
            if(op!=7){ if(dir){ if(w)*(uint16_t*)get_reg_ptr(vm, m.reg, 1)=res; else *(uint8_t*)get_reg_ptr(vm, m.reg, 0)=res; } else rm_write(vm, &m, w, res); }
            break;
        }
        case 0x04: case 0x05: case 0x0C: case 0x0D: case 0x24: case 0x25: case 0x2C: case 0x2D: case 0x34: case 0x35: case 0x3C: case 0x3D:
        {
            w=opcode&1; dst=w?vm->cpu.ax:(vm->cpu.ax&0xFF); src=w?fetch16(vm):fetch8(vm);
            int op=(opcode>>3)&7;
            switch(op){
                case 0: res=dst+src; update_arith_flags(vm, res, src, dst, 0, w?16:8); break;
                case 1: res=dst|src; update_logic_flags(vm, res, w?16:8); break;
                case 4: res=dst&src; update_logic_flags(vm, res, w?16:8); break;
                case 5: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
                case 6: res=dst^src; update_logic_flags(vm, res, w?16:8); break;
                case 7: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
            }
            if(op!=7){ if(w)vm->cpu.ax=res; else vm->cpu.ax=(vm->cpu.ax&0xFF00)|(res&0xFF); }
            break;
        }
        case 0x80 ... 0x83:
        {
             w=opcode&1; int s=(opcode==0x83); decode_modrm(vm, &m); dst=rm_read(vm, &m, w);
             if(s) src=(int16_t)(int8_t)fetch8(vm); else if(w) src=fetch16(vm); else src=fetch8(vm);
             switch(m.reg){
                 case 0: res=dst+src; update_arith_flags(vm, res, src, dst, 0, w?16:8); break;
                 case 5: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
                 case 7: res=dst-src; update_arith_flags(vm, res, src, dst, 1, w?16:8); break;
                 default: res=dst;
             }
             if(m.reg!=7) rm_write(vm, &m, w, res);
             break;
        }
        case 0x40 ... 0x47: dst=*(uint16_t*)get_reg_ptr(vm, opcode&7, 1); res=dst+1; *(uint16_t*)get_reg_ptr(vm, opcode&7, 1)=res; update_arith_flags(vm, res, 1, dst, 0, 16); break;
        case 0x48 ... 0x4F: dst=*(uint16_t*)get_reg_ptr(vm, opcode&7, 1); res=dst-1; *(uint16_t*)get_reg_ptr(vm, opcode&7, 1)=res; update_arith_flags(vm, res, 1, dst, 1, 16); break;
        case 0x8D: decode_modrm(vm, &m); *(uint16_t*)get_reg_ptr(vm, m.reg, 1)=m.offset; break;
        case 0xEB: vm->cpu.ip+=(int8_t)fetch8(vm); break;
        case 0xE9: vm->cpu.ip+=(int16_t)fetch16(vm); break;
        case 0xE8: src=fetch16(vm); push16(vm, vm->cpu.ip); vm->cpu.ip+=(int16_t)src; break;
        case 0xC3: vm->cpu.ip=pop16(vm); break;
        case 0x74: { int8_t r=(int8_t)fetch8(vm); if(vm->cpu.flags&FLAG_ZF) vm->cpu.ip+=r; break; }
        case 0x75: { int8_t r=(int8_t)fetch8(vm); if(!(vm->cpu.flags&FLAG_ZF)) vm->cpu.ip+=r; break; }
        case 0xE4: fetch8(vm); while(!keyboard_has_key()){} vm->cpu.ax=(vm->cpu.ax&0xFF00)|(uint8_t)keyboard_getchar(); break;
        case 0xE6: { uint8_t p=fetch8(vm); if(p==0x10) { __asm__ volatile("cli"); tty_putchar(vm->cpu.ax&0xFF); __asm__ volatile("sti"); } break; }
        case 0xAC: vm->cpu.ax=(vm->cpu.ax&0xFF00)|mem_read8(vm, vm->cpu.ds, vm->cpu.si); if(vm->cpu.flags&FLAG_DF)vm->cpu.si--;else vm->cpu.si++; break;
        case 0xF4: if(current_run) current_run->exit_reason=KVM_EXIT_HLT; return 1;

        default:
            tty_putstr("\n[VM] Unk Op: "); tty_puthex(opcode); tty_putstr("\n");
            if(current_run) current_run->exit_reason=KVM_EXIT_HLT; return 1;
    }
    return 0;
}

int danos_kvm_run(int vcpu_fd) {
    (void)vcpu_fd; if(!current_vm) return -1;
    static int cycle_limit=0;
    for(int i=0; i<1000; i++) { if(emulate_instruction(current_vm)!=0) return 0; }
    cycle_limit++; if(cycle_limit>5000) { if(current_run) current_run->exit_reason=KVM_EXIT_HLT; }
    return 0;
}

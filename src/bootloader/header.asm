section .header

boot_start:
    dd 0xe85250d6                                                   ; multiboot2 magic number
    dd 0                                                            ; protected mode i386
    dd header_end - boot_start                                      ; header length
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - boot_start))   ; checksum

; =============================================================================
; Framebuffer Request Tag
; =============================================================================
align 8
framebuffer_tag:
    dw 5                                    ; type = framebuffer tag
    dw 1                                    ; flags = 1 (optional - boot even if not available)
    dd 20                                   ; size of this tag (fixed at 20 bytes)
    dd 1280                                 ; preferred width
    dd 720                                  ; preferred height  
    dd 32                                   ; preferred bpp

; =============================================================================
; End Tag (required to terminate tag list)
; =============================================================================
align 8
    dw 0                                    ; type = end
    dw 0                                    ; flags
    dd 8                                    ; size

header_end:
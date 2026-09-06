@ svcControlService : extension kernel Luma3DS (SVC 0xB0).
@ Extrait de Luma3DS sysmodules/rosalina/source/csvc.s (LumaTeam/Luma3DS),
@ même usage que le launcher de zaksabeast/3ds-Plug-n-play et PKSM.
@ Licence d'origine (zlib-like, cf en-tête de csvc.s dans Luma3DS) :
@   This software is provided 'as-is', without any express or implied warranty.
@   Altered source versions must be plainly marked as such.

.arm
.balign 4

.macro SVC_BEGIN name
    .section .text.\name, "ax", %progbits
    .global \name
    .type \name, %function
    .align 2
    .cfi_startproc
\name:
.endm

.macro SVC_END
    .cfi_endproc
.endm

SVC_BEGIN svcControlService
    svc 0xB0
    bx lr
SVC_END

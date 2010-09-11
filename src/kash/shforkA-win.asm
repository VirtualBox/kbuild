; $Id: shforkA-win.asm 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $
;; @file
; shforkA-win.asm - assembly routines used when forking on Windows.
;

;
; Copyright (c) 2009-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
;
; This file is part of kBuild.
;
; kBuild is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 3 of the License, or
; (at your option) any later version.
;
; kBuild is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with kBuild.  If not, see <http://www.gnu.org/licenses/>
;
;

;*******************************************************************************
;*      Defined Constants And Macros                                           *
;*******************************************************************************
%ifdef KBUILD_ARCH_AMD64
 %define NAME(name) name
%else
 %define NAME(name) _ %+ name
%endif

;; The stack size. This is also defined in shfork-win.c.
%define SHFORK_STACK_SIZE (1*1024*1024)


;*******************************************************************************
;*      External Symbols                                                       *
;*******************************************************************************
extern NAME(real_main)
extern NAME(shfork_maybe_forked)
extern NAME(shfork_body)


[section .text]

;;
; C main() wrapper.
;
NAME(main):
global NAME(main)
%ifdef KBUILD_ARCH_AMD64
[proc_frame main]
%endif

        ;
        ; Prolog, spilling parameters from registers.
        ;
%ifdef KBUILD_ARCH_AMD64
        [pushreg rbp]
        push    rbp
        [setframe rbp, 0]
        mov     rbp, rsp
        [allocstack 0x40]
        sub     rsp, 40h
        and     rsp, ~1fh
        mov     [rbp-08h], rcx          ; argc
        mov     [rbp-10h], rdx          ; argv
        mov     [rbp-18h], r8           ; envp
        [endprolog]
%else
        push    ebp
        mov     ebp, esp
        sub     esp, 40h
        and     esp, ~1fh
%endif

        ;
        ; Call shfork_maybe_forked. This will not return if we're forking.
        ;
%ifndef KBUILD_ARCH_AMD64
        mov     ecx, [ebp +  8h]        ; argc
        mov     edx, [ebp + 0ch]        ; argv
        mov     eax, [ebp + 10h]        ; envp
        mov     [esp     ], ecx
        mov     [esp + 4h], edx
        mov     [esp + 8h], eax
%endif
        call    NAME(shfork_maybe_forked)

        ;
        ; Ok, it returned which means we're not forking.
        ;
        ; The accumulator register is now pointing to the top of the
        ; stack we're going to call real_main on. Switch and call it.
        ;
        ; The TIB adjustments is required or we'll crash in longjmp/unwind.
        ;
%ifdef KBUILD_ARCH_AMD64
        mov     [rsp + 18h], rax
        mov     [rax -  8h], rsp

        mov     r10, [gs:08h]           ; StackBase  (the higher value)
        mov     r11, [gs:10h]           ; StackLimit (the lower value)
        mov     [rax - 10h], r10
        mov     [rax - 18h], r11
        cmp     rax, r10
        jb      .below
        mov     [gs:08h], rax
.below:
        lea     r9, [rax - SHFORK_STACK_SIZE]
        cmp     r9, r11
        ja      .above
        mov     [gs:10h], r9
.above:

        mov     rcx, [rbp - 08h]        ; argc
        mov     rdx, [rbp - 10h]        ; argv
        mov     r8,  [rbp - 18h]        ; envp

        lea     rsp, [eax - 40h]        ; Switch!
%else
        mov     [esp + 18h], eax
        mov     [eax - 4], esp
        lea     esp, [eax - 40h]        ; Switch!

        mov     edx, [fs:04h]           ; StackBase  (the higher value)
        mov     ecx, [fs:08h]           ; StackLimit (the lower value)
        mov     [eax - 10h], edx
        mov     [eax - 18h], ecx
        cmp     eax, edx
        jb      .below
        mov     [fs:04h], eax
.below:
        lea     edx, [eax - SHFORK_STACK_SIZE]
        cmp     edx, ecx
        ja      .above
        mov     [fs:08h], edx
.above:

        mov     ecx, [ebp +  8h]        ; argc
        mov     edx, [ebp + 0ch]        ; argv
        mov     eax, [ebp + 10h]        ; envp

        mov     [esp     ], ecx
        mov     [esp + 4h], edx
        mov     [esp + 8h], eax
%endif
        call    NAME(real_main)

        ;
        ; Switch back the stack, restore the TIB fields and we're done.
        ;
%ifdef KBUILD_ARCH_AMD64
        lea     r11, [rsp + 40h]
        mov     rsp, [rsp + 38h]
        mov     r8, [r11 - 10h]
        mov     r9, [r11 - 18h]
        mov     [gs:08h], r8
        mov     [gs:10h], r9
%else
        lea     edx, [esp + 40h]
        mov     esp, [esp + 2ch]
        mov     ecx, [edx - 10h]
        mov     edx, [edx - 18h]
        mov     [fs:04h], ecx
        mov     [fs:08h], edx
%endif
        leave
        ret
%ifdef KBUILD_ARCH_AMD64
[endproc_frame main]
%endif


;;
; sh_fork() worker
;
; @returns      See fork().
; @param        psh
;
NAME(shfork_do_it):
global NAME(shfork_do_it)
%ifdef KBUILD_ARCH_AMD64
        [proc_frame shfork_do_it]
        [pushreg rbp]
        push    rbp
        [setframe rbp, 0]
        mov     rbp, rsp
        [allocstack 0x400]
        sub     rsp, 400h
        and     rsp, ~1ffh
[endprolog]
%else
        push    ebp
        mov     ebp, esp
        sub     esp, 400h
        and     esp, ~1ffh
%endif

        ;
        ; Save most registers so they can be restored in the child.
        ;
%ifdef KBUILD_ARCH_AMD64
        fxsave  [rsp]
        mov     [rsp + 200h], rbp
        mov     [rsp + 208h], rax
        mov     [rsp + 210h], rbx
        mov     [rsp + 218h], rcx
        mov     [rsp + 220h], rdx
        mov     [rsp + 228h], rsi
        mov     [rsp + 230h], rdi
        mov     [rsp + 238h],  r8
        mov     [rsp + 240h],  r9
        mov     [rsp + 248h], r10
        mov     [rsp + 250h], r11
        mov     [rsp + 258h], r12
        mov     [rsp + 260h], r13
        mov     [rsp + 268h], r14
        mov     [rsp + 270h], r15
%else
        fxsave  [esp]
        mov     [esp + 200h], ebp
        mov     [esp + 208h], eax
        mov     [esp + 210h], ebx
        mov     [esp + 218h], ecx
        mov     [esp + 220h], edx
        mov     [esp + 228h], esi
        mov     [esp + 230h], edi
%endif

        ;
        ; Call the shfork_body that will spawn the child and all that.
        ;
%ifdef KBUILD_ARCH_AMD64
        ;mov     rcx, rcx               ; psh
        mov     rdx, rsp                ; stack_ptr
        sub     rsp, 20h
        call    NAME(shfork_body)
        lea     rsp, [rsp + 20h]
%else
        mov     edx, esp
        mov     ecx, [ebp + 8h]         ; psh
        sub     esp, 20h
        mov     [esp    ], ecx
        mov     [esp + 4], edx          ; stack_ptr
        call    NAME(shfork_body)
        lea     esp, [esp + 20h]
%endif

        ;
        ; Just leave the function, no need to restore things.
        ;
        leave
        ret
%ifdef KBUILD_ARCH_AMD64
[endproc_frame shfork_do_it]
%endif


;;
; Switch the stack, restore the register and leave as if we'd called shfork_do_it.
;
; @param        cur     Current stack pointer.
; @param        base    The stack base  (higher value).
; @param        limit   The stack limit (lower value).
;
NAME(shfork_resume):
global NAME(shfork_resume)
%ifdef KBUILD_ARCH_AMD64
        mov     rsp, rcx
%else
        mov     ecx, [esp + 4]
        mov     edx, [esp + 8]
        mov     eax, [esp + 12]
        mov     esp, ecx
%endif

        ;
        ; Adjust stack stuff in the TIB (longjmp/unwind).
        ;
%ifdef KBUILD_ARCH_AMD64
        cmp     rdx, [gs:08h]           ; StackBase  (the higher value)
        jb      .below
        mov     [gs:08h], rdx
.below:
        cmp     r8,  [gs:10h]           ; StackLimit
        ja      .above
        mov     [gs:10h], r8
.above:
%else
        cmp     edx, [fs:04h]           ; StackBase  (the higher value)
        jb      .below
        mov     [fs:04h], edx
.below:
        cmp     eax, [fs:08h]           ; StackLimit
        ja      .above
        mov     [fs:08h], eax
.above:
%endif

        ;
        ; Restore most of the registers.
        ;
        ;; @todo xmm registers may require explicit saving/restoring...
%ifdef KBUILD_ARCH_AMD64
        frstor  [rsp]
        mov     rbp, [rsp + 200h]
        mov     rax, [rsp + 208h]
        mov     rbx, [rsp + 210h]
        mov     rcx, [rsp + 218h]
        mov     rdx, [rsp + 220h]
        mov     rsi, [rsp + 228h]
        mov     rdi, [rsp + 230h]
        mov      r8, [rsp + 238h]
        mov      r9, [rsp + 240h]
        mov     r10, [rsp + 248h]
        mov     r11, [rsp + 250h]
        mov     r12, [rsp + 258h]
        mov     r13, [rsp + 260h]
        mov     r14, [rsp + 268h]
        mov     r15, [rsp + 270h]
%else
        frstor  [esp]
        mov     ebp, [esp + 200h]
        mov     eax, [esp + 208h]
        mov     ebx, [esp + 210h]
        mov     ecx, [esp + 218h]
        mov     edx, [esp + 220h]
        mov     esi, [esp + 228h]
        mov     edi, [esp + 230h]
%endif
        xor     eax, eax                ; the child returns 0.
        leave
        ret


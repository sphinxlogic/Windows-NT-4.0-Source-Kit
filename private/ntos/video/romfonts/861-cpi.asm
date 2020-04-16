;/*
; *                      Microsoft Confidential
; *                      Copyright (C) Microsoft Corporation 1991
; *                      All Rights Reserved.
; */
CODE    SEGMENT BYTE PUBLIC 'CODE'
        ASSUME CS:CODE,DS:CODE

IF1
;        %OUT    EGA.CPI creation file
;        %OUT    .
;        %OUT    CP SRC files:
;        %OUT    .
;	 %OUT	 .	 CODE PAGE:  861
ENDIF

EGA861: DW     LEN_861			; SIZE OF ENTRY HEADER
	DW     POST_EGA861,0		; POINTER TO NEXT HEADER
        DW     1                        ; DEVICE TYPE
        DB     "EGA     "               ; DEVICE SUBTYPE ID
	DW     861			; CODE PAGE ID
        DW     3 DUP(0)                 ; RESERVED
	DW     OFFSET DATA861,0 	; POINTER TO FONTS
LEN_861 EQU    ($-EGA861)		;
                                        ;
DATA861:DW     1			; CART/NON-CART
        DW     3                        ; # OF FONTS
	DW     LEN_D861 		; LENGTH OF DATA
D861:					;
        DB     16,8                     ; CHARACTER BOX SIZE
        DB     0,0                      ; ASPECT RATIO (UNUSED)
        DW     256                      ; NUMBER OF CHARACTERS
                                        ;
	INCLUDE 861-8X16.ASM		;
                                        ;
        DB     14,8                     ; CHARACTER BOX SIZE
        DB     0,0                      ; ASPECT RATIO (UNUSED)
        DW     256                      ; NUMBER OF CHARACTERS
                                        ;
	INCLUDE 861-8X14.ASM		;
                                        ;
        DB     8,8                      ; CHARACTER BOX SIZE
        DB     0,0                      ; ASPECT RATIO (UNUSED)
        DW     256                      ; NUMBER OF CHARACTERS
                                        ;
	INCLUDE 861-8X8.ASM		;
                                        ;
LEN_D861	EQU ($-D861)		;
                                        ;
POST_EGA861	EQU	$		;
                                        ;
CODE    ENDS                            ;
        END                             ;



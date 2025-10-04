// ===============================================================
// SNAKE para Nand2Tetris (Hack Assembly) - VERSION CORREGIDA
// - Serpiente de 16 segmentos fija (16 "bits").
// - Inicia en lateral izquierdo, a mitad de alto (y=128).
// - Control: W/A/S/D (mayús o minús), avanza 1 paso por tecla.
// - Manzana se fija ANTES de ejecutar (editar appleX/appleY en RAM).
// - Si toca borde o manzana: CLEARSCREEN y bucle FINAL.
// ===============================================================

// --------- Constantes / símbolos de I/O y dimensiones ----------
@16384      // base SCREEN
D=A
@SCREEN
M=D

@24576      // addr KEYBOARD
D=A
@KEYBD
M=D

@512
D=A
@WIDTH
M=D

@256
D=A
@HEIGHT
M=D

@16
D=A
@LEN
M=D

@15
D=A
@LENM1
M=D

// Dirección inicial: (1,0)
@1
D=A
@dirX
M=D
@dirY
M=0

// Manzana (puedes dejar estos por defecto y/o EDITAR EN RAM)
@40
D=A
@appleX
M=D
@10
D=A
@appleY
M=D

// Bases de los arreglos en RAM (fijas)
@300
D=A
@XBASE
M=D       // snakeX[0]..[15] en 300..315
@320
D=A
@YBASE
M=D       // snakeY[0]..[15] en 320..335

@INIT
0;JMP

// =================== SUBRUTINAS ===================
// RET: registro de retorno
// R13: x (temporal para SETPIXEL)
// R14: y (temporal para SETPIXEL)

// SETPIXEL: pinta (x=R13, y=R14) a 1
// addr = SCREEN + y*32 + floor(x/16)
// bit  = x % 16
(SETPIXEL)
    // rowOff = y * 32 (y << 5) por duplicaciones
    @R14
    D=M
    @rowOff
    M=D        // rowOff = y
    @rowOff
    D=M
    D=D+M      // *2
    D=D+M      // *4
    D=D+M      // *8
    D=D+M      // *16
    D=D+M      // *32
    @rowOff
    M=D

    // wordOff = floor(x/16), bitPos = x % 16
    @R13
    D=M
    @xTmp
    M=D
    @wordOff
    M=0
(DIV16_LOOP)
    @xTmp
    D=M
    @16
    D=D-A
    @DIV16_END
    D;JLT
    @xTmp
    M=D
    @wordOff
    M=M+1
    @DIV16_LOOP
    0;JMP
(DIV16_END)

    // addr = SCREEN + rowOff + wordOff
    @SCREEN
    D=M
    @rowOff
    D=D+M
    @wordOff
    D=D+M
    @addr
    M=D

    // mask = 1 << bitPos
    @xTmp      // x % 16 quedó en xTmp
    D=M
    @bitPos
    M=D
    @1
    D=A
    @mask
    M=D
(MASK_LOOP)
    @bitPos
    D=M
    @MASK_DONE
    D;JEQ
    @mask      // mask = mask + mask (doblar)
    D=M
    M=D+M
    @bitPos
    M=M-1
    @MASK_LOOP
    0;JMP
(MASK_DONE)

    // *addr |= mask
    @addr
    A=M
    D=M
    @mask
    D=D|M
    @addr
    A=M
    M=D

    @RET
    A=M
    0;JMP

// CLEARSCREEN: 8192 palabras a 0
(CLEARSCREEN)
    @8192
    D=A
    @csCnt
    M=D
    @SCREEN
    D=M
    @p
    M=D
(CS_LOOP)
    @csCnt
    D=M
    @CS_DONE
    D;JEQ
    @p
    A=M
    M=0
    @p
    M=M+1
    @csCnt
    M=M-1
    @CS_LOOP
    0;JMP
(CS_DONE)
    @RET
    A=M
    0;JMP

// DRAWAPPLE: (appleX, appleY)
(DRAWAPPLE)
    @appleX
    D=M
    @R13
    M=D
    @appleY
    D=M
    @R14
    M=D
    @AFTER_SETPIX_APP
    D=A
    @RET
    M=D
    @SETPIXEL
    0;JMP
(AFTER_SETPIX_APP)
    @RET
    A=M
    0;JMP

// DRAWSNAKE: i=0..15 -> pinta snake[i]
(DRAWSNAKE)
    @0
    D=A
    @i
    M=D
(DS_LOOP)
    @i
    D=M
    @LEN
    D=D-M
    @DS_END
    D;JGE

    // R13 = snakeX[i]
    @XBASE
    D=M
    @i
    A=D+M
    D=M
    @R13
    M=D

    // R14 = snakeY[i]
    @YBASE
    D=M
    @i
    A=D+M
    D=M
    @R14
    M=D

    // call SETPIXEL
    @AFTER_SETPIX_DS
    D=A
    @RET
    M=D
    @SETPIXEL
    0;JMP
(AFTER_SETPIX_DS)
    @i
    M=M+1
    @DS_LOOP
    0;JMP
(DS_END)
    @RET
    A=M
    0;JMP

// WAITKEY: bloquea hasta KEYBOARD != 0 y guarda ASCII en key
(WAITKEY)
(WK_LOOP)
    @KEYBD
    A=M
    D=M
    @WK_LOOP
    D;JEQ
    @key
    M=D
    @RET
    A=M
    0;JMP

// UPDATEDIR: W/A/S/D -> dirX, dirY
(UPDATEDIR)
    @key
    D=M
    @87        // 'W'
    D=D-A
    @SET_UP
    D;JEQ
    @key
    D=M
    @119       // 'w'
    D=D-A
    @SET_UP
    D;JEQ

    @key
    D=M
    @83        // 'S'
    D=D-A
    @SET_DOWN
    D;JEQ
    @key
    D=M
    @115       // 's'
    D=D-A
    @SET_DOWN
    D;JEQ

    @key
    D=M
    @65        // 'A'
    D=D-A
    @SET_LEFT
    D;JEQ
    @key
    D=M
    @97        // 'a'
    D=D-A
    @SET_LEFT
    D;JEQ

    @key
    D=M
    @68        // 'D'
    D=D-A
    @SET_RIGHT
    D;JEQ
    @key
    D=M
    @100       // 'd'
    D=D-A
    @SET_RIGHT
    D;JEQ
    @UD_END
    0;JMP

(SET_UP)
    @0
    D=A
    @dirX
    M=D
    @-1
    D=A
    @dirY
    M=D
    @UD_END
    0;JMP

(SET_DOWN)
    @0
    D=A
    @dirX
    M=D
    @1
    D=A
    @dirY
    M=D
    @UD_END
    0;JMP

(SET_LEFT)
    @-1
    D=A
    @dirX
    M=D
    @0
    D=A
    @dirY
    M=D
    @UD_END
    0;JMP

(SET_RIGHT)
    @1
    D=A
    @dirX
    M=D
    @0
    D=A
    @dirY
    M=D
(UD_END)
    @RET
    A=M
    0;JMP

// MOVESNAKE: desplaza cuerpo y pone nueva cabeza
(MOVESNAKE)
    // newX = snakeX0 + dirX
    @XBASE
    A=M
    D=M
    @dirX
    D=D+M
    @newX
    M=D
    // newY = snakeY0 + dirY
    @YBASE
    A=M
    D=M
    @dirY
    D=D+M
    @newY
    M=D

    // j = LEN-1 .. 1
    @LENM1
    D=M
    @j
    M=D
(MS_LOOP)
    @j
    D=M
    @MS_DONE
    D;JEQ

    // snakeX[j] = snakeX[j-1]
    @XBASE
    D=M
    @j
    A=D+M      // &snakeX[j]
    D=A
    @tmpAddr
    M=D
    @j
    D=M-1
    @XBASE
    A=M
    A=A+D      // &snakeX[j-1]
    D=M
    @tmpAddr
    A=M
    M=D

    // snakeY[j] = snakeY[j-1]
    @YBASE
    D=M
    @j
    A=D+M
    D=A
    @tmpAddr
    M=D
    @j
    D=M-1
    @YBASE
    A=M
    A=A+D
    D=M
    @tmpAddr
    A=M
    M=D

    // j--
    @j
    M=M-1
    @MS_LOOP
    0;JMP
(MS_DONE)
    // nueva cabeza
    @newX
    D=M
    @XBASE
    A=M
    M=D
    @newY
    D=M
    @YBASE
    A=M
    M=D
    @RET
    A=M
    0;JMP

// CHECKCOL: col=1 si fuera de límites o igual a manzana
(CHECKCOL)
    @0
    D=A
    @col
    M=D
    // x<0 ?
    @XBASE
    A=M
    D=M
    @COL_YNEG
    D;JLT
    // x>=WIDTH ?
    @XBASE
    A=M
    D=M
    @WIDTH
    D=D-M
    @SETCOL
    D;JGE
(COL_YNEG)
    // y<0 ?
    @YBASE
    A=M
    D=M
    @COL_YBND
    D;JLT
    // y>=HEIGHT ?
    @YBASE
    A=M
    D=M
    @HEIGHT
    D=D-M
    @SETCOL
    D;JGE
(COL_YBND)
    // manzana?
    @XBASE
    A=M
    D=M
    @appleX
    D=D-M
    @NO_APPLE
    D;JNE
    @YBASE
    A=M
    D=M
    @appleY
    D=D-M
    @NO_APPLE
    D;JNE
    @SETCOL
    0;JMP
(NO_APPLE)
    @RET
    A=M
    0;JMP
(SETCOL)
    @1
    D=A
    @col
    M=D
    @RET
    A=M
    0;JMP

// DRAWALL: clear + apple + snake
(DRAWALL)
    @AFTER_CLEAR
    D=A
    @RET
    M=D
    @CLEARSCREEN
    0;JMP
(AFTER_CLEAR)
    @AFTER_APPLE
    D=A
    @RET
    M=D
    @DRAWAPPLE
    0;JMP
(AFTER_APPLE)
    @AFTER_SNAKE
    D=A
    @RET
    M=D
    @DRAWSNAKE
    0;JMP
(AFTER_SNAKE)
    @RET
    A=M
    0;JMP

// =================== INIT & GAME ===================
(INIT)
    // y medio = 128
    @128
    D=A
    @halfY
    M=D
    // i=0
    @0
    D=A
    @i
    M=D
(INIT_LOOP)
    @i
    D=M
    @LEN
    D=D-M
    @INIT_DONE
    D;JGE

    // x = LEN-1 - i
    @LENM1
    D=M
    @i
    D=D-M
    @tmpX
    M=D
    // y = halfY
    @halfY
    D=M
    @tmpY
    M=D

    // snakeX[i] = x
    @XBASE
    D=M
    @i
    A=D+M
    @tmpX
    D=M
    M=D
    // snakeY[i] = y
    @YBASE
    D=M
    @i
    A=D+M
    @tmpY
    D=M
    M=D

    @i
    M=M+1
    @INIT_LOOP
    0;JMP
(INIT_DONE)
    // Primer dibujado
    @AFTER_DRAW_INIT
    D=A
    @RET
    M=D
    @DRAWALL
    0;JMP
(AFTER_DRAW_INIT)
    @GAME
    0;JMP

(GAME)
    @AFTER_WAIT
    D=A
    @RET
    M=D
    @WAITKEY
    0;JMP
(AFTER_WAIT)
    @AFTER_UPDATE
    D=A
    @RET
    M=D
    @UPDATEDIR
    0;JMP
(AFTER_UPDATE)
    @AFTER_MOVE
    D=A
    @RET
    M=D
    @MOVESNAKE
    0;JMP
(AFTER_MOVE)
    @AFTER_CHECK
    D=A
    @RET
    M=D
    @CHECKCOL
    0;JMP
(AFTER_CHECK)
    @col
    D=M
    @COLLISION
    D;JNE
    @AFTER_DRAW
    D=A
    @RET
    M=D
    @DRAWALL
    0;JMP
(AFTER_DRAW)
    @GAME
    0;JMP

(COLLISION)
    @AFTER_CLEAR2
    D=A
    @RET
    M=D
    @CLEARSCREEN
    0;JMP
(AFTER_CLEAR2)
    @FINAL
    0;JMP

(FINAL)
    @FINAL
    0;JMP

// =================== Variables ===================
(dirX)
(dirY)
(appleX)
(appleY)
(key)
(newX)
(newY)
(i)
(j)
(tmpX)
(tmpY)
(rowOff)
(wordOff)
(bitPos)
(mask)
(addr)
(xTmp)
(tmpAddr)
(col)
(csCnt)
(halfY)
(XBASE)
(YBASE)
(WIDTH)
(HEIGHT)
(LEN)
(LENM1)
(SCREEN)
(KEYBD)
(RET)
(p)
//
// Created by ASUS on 28/09/2025.
//

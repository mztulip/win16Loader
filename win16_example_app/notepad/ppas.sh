#!/bin/sh
DoExitAsm ()
{ echo "An error occurred while assembling $1"; exit 1; }
DoExitLink ()
{ echo "An error occurred while linking $1"; exit 1; }
echo Assembling 'notepad(1)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s1.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s1.s
if [ $? != 0 ]; then DoExitAsm 'notepad(1)'; fi
echo Assembling 'notepad(2)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s2.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s2.s
if [ $? != 0 ]; then DoExitAsm 'notepad(2)'; fi
echo Assembling 'notepad(3)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s3.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s3.s
if [ $? != 0 ]; then DoExitAsm 'notepad(3)'; fi
echo Assembling 'notepad(4)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s4.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s4.s
if [ $? != 0 ]; then DoExitAsm 'notepad(4)'; fi
echo Assembling 'notepad(5)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s5.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s5.s
if [ $? != 0 ]; then DoExitAsm 'notepad(5)'; fi
echo Assembling 'notepad(6)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s6.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s6.s
if [ $? != 0 ]; then DoExitAsm 'notepad(6)'; fi
echo Assembling 'notepad(7)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s7.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s7.s
if [ $? != 0 ]; then DoExitAsm 'notepad(7)'; fi
echo Assembling 'notepad(8)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s8.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s8.s
if [ $? != 0 ]; then DoExitAsm 'notepad(8)'; fi
echo Assembling 'notepad(9)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s9.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s9.s
if [ $? != 0 ]; then DoExitAsm 'notepad(9)'; fi
echo Assembling 'notepad(10)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s10.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s10.s
if [ $? != 0 ]; then DoExitAsm 'notepad(10)'; fi
echo Assembling 'notepad(11)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s11.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s11.s
if [ $? != 0 ]; then DoExitAsm 'notepad(11)'; fi
echo Assembling 'notepad(12)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s12.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s12.s
if [ $? != 0 ]; then DoExitAsm 'notepad(12)'; fi
echo Assembling 'notepad(13)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s13.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s13.s
if [ $? != 0 ]; then DoExitAsm 'notepad(13)'; fi
echo Assembling 'notepad(14)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s14.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s14.s
if [ $? != 0 ]; then DoExitAsm 'notepad(14)'; fi
echo Assembling 'notepad(15)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s15.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s15.s
if [ $? != 0 ]; then DoExitAsm 'notepad(15)'; fi
echo Assembling 'notepad(16)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s16.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s16.s
if [ $? != 0 ]; then DoExitAsm 'notepad(16)'; fi
echo Assembling 'notepad(17)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s17.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s17.s
if [ $? != 0 ]; then DoExitAsm 'notepad(17)'; fi
echo Assembling 'notepad(18)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s18.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s18.s
if [ $? != 0 ]; then DoExitAsm 'notepad(18)'; fi
echo Assembling 'notepad(19)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s19.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s19.s
if [ $? != 0 ]; then DoExitAsm 'notepad(19)'; fi
echo Assembling 'notepad(20)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s20.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s20.s
if [ $? != 0 ]; then DoExitAsm 'notepad(20)'; fi
echo Assembling 'notepad(21)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s21.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s21.s
if [ $? != 0 ]; then DoExitAsm 'notepad(21)'; fi
echo Assembling 'notepad(22)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s22.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s22.s
if [ $? != 0 ]; then DoExitAsm 'notepad(22)'; fi
echo Assembling 'notepad(23)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s23.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s23.s
if [ $? != 0 ]; then DoExitAsm 'notepad(23)'; fi
echo Assembling 'notepad(24)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s24.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s24.s
if [ $? != 0 ]; then DoExitAsm 'notepad(24)'; fi
echo Assembling 'notepad(25)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s25.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s25.s
if [ $? != 0 ]; then DoExitAsm 'notepad(25)'; fi
echo Assembling 'notepad(26)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s26.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s26.s
if [ $? != 0 ]; then DoExitAsm 'notepad(26)'; fi
echo Assembling 'notepad(27)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s27.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s27.s
if [ $? != 0 ]; then DoExitAsm 'notepad(27)'; fi
echo Assembling 'notepad(28)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s28.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s28.s
if [ $? != 0 ]; then DoExitAsm 'notepad(28)'; fi
echo Assembling 'notepad(29)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s29.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s29.s
if [ $? != 0 ]; then DoExitAsm 'notepad(29)'; fi
echo Assembling 'notepad(30)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s30.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s30.s
if [ $? != 0 ]; then DoExitAsm 'notepad(30)'; fi
echo Assembling 'notepad(31)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s31.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s31.s
if [ $? != 0 ]; then DoExitAsm 'notepad(31)'; fi
echo Assembling 'notepad(32)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s32.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s32.s
if [ $? != 0 ]; then DoExitAsm 'notepad(32)'; fi
echo Assembling 'notepad(33)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s33.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s33.s
if [ $? != 0 ]; then DoExitAsm 'notepad(33)'; fi
echo Assembling 'notepad(34)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s34.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s34.s
if [ $? != 0 ]; then DoExitAsm 'notepad(34)'; fi
echo Assembling 'notepad(35)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s35.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s35.s
if [ $? != 0 ]; then DoExitAsm 'notepad(35)'; fi
echo Assembling 'notepad(36)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s36.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s36.s
if [ $? != 0 ]; then DoExitAsm 'notepad(36)'; fi
echo Assembling 'notepad(37)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s37.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s37.s
if [ $? != 0 ]; then DoExitAsm 'notepad(37)'; fi
echo Assembling 'notepad(38)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s38.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s38.s
if [ $? != 0 ]; then DoExitAsm 'notepad(38)'; fi
echo Assembling 'notepad(39)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s39.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s39.s
if [ $? != 0 ]; then DoExitAsm 'notepad(39)'; fi
echo Assembling 'notepad(40)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s40.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s40.s
if [ $? != 0 ]; then DoExitAsm 'notepad(40)'; fi
echo Assembling 'notepad(41)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s41.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s41.s
if [ $? != 0 ]; then DoExitAsm 'notepad(41)'; fi
echo Assembling 'notepad(42)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s42.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s42.s
if [ $? != 0 ]; then DoExitAsm 'notepad(42)'; fi
echo Assembling 'notepad(43)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s43.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s43.s
if [ $? != 0 ]; then DoExitAsm 'notepad(43)'; fi
echo Assembling 'notepad(44)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s44.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s44.s
if [ $? != 0 ]; then DoExitAsm 'notepad(44)'; fi
echo Assembling 'notepad(45)'
/usr/bin/nasm -f obj -o NOTEPAD.sl/NOTEPAD0s45.o -w-orphan-labels  NOTEPAD.sl/NOTEPAD0s45.s
if [ $? != 0 ]; then DoExitAsm 'notepad(45)'; fi
OFS=$IFS
IFS="
"
/tmp/watcom/binl64/wlib -q -p=16 -fo -c -b -n -o=NOTEPAD.a.tmp NOTEPAD.a  +NOTEPAD.sl/NOTEPAD0s45.o +NOTEPAD.sl/NOTEPAD0s44.o +NOTEPAD.sl/NOTEPAD0s43.o +NOTEPAD.sl/NOTEPAD0s42.o +NOTEPAD.sl/NOTEPAD0s41.o +NOTEPAD.sl/NOTEPAD0s40.o +NOTEPAD.sl/NOTEPAD0s39.o +NOTEPAD.sl/NOTEPAD0s38.o +NOTEPAD.sl/NOTEPAD0s37.o +NOTEPAD.sl/NOTEPAD0s36.o +NOTEPAD.sl/NOTEPAD0s35.o +NOTEPAD.sl/NOTEPAD0s34.o +NOTEPAD.sl/NOTEPAD0s33.o +NOTEPAD.sl/NOTEPAD0s32.o +NOTEPAD.sl/NOTEPAD0s31.o +NOTEPAD.sl/NOTEPAD0s30.o +NOTEPAD.sl/NOTEPAD0s29.o +NOTEPAD.sl/NOTEPAD0s28.o +NOTEPAD.sl/NOTEPAD0s27.o +NOTEPAD.sl/NOTEPAD0s26.o +NOTEPAD.sl/NOTEPAD0s25.o +NOTEPAD.sl/NOTEPAD0s24.o +NOTEPAD.sl/NOTEPAD0s23.o +NOTEPAD.sl/NOTEPAD0s22.o +NOTEPAD.sl/NOTEPAD0s21.o +NOTEPAD.sl/NOTEPAD0s20.o +NOTEPAD.sl/NOTEPAD0s19.o +NOTEPAD.sl/NOTEPAD0s18.o +NOTEPAD.sl/NOTEPAD0s17.o +NOTEPAD.sl/NOTEPAD0s16.o +NOTEPAD.sl/NOTEPAD0s15.o +NOTEPAD.sl/NOTEPAD0s14.o +NOTEPAD.sl/NOTEPAD0s13.o +NOTEPAD.sl/NOTEPAD0s12.o +NOTEPAD.sl/NOTEPAD0s11.o +NOTEPAD.sl/NOTEPAD0s10.o +NOTEPAD.sl/NOTEPAD0s9.o +NOTEPAD.sl/NOTEPAD0s8.o +NOTEPAD.sl/NOTEPAD0s7.o +NOTEPAD.sl/NOTEPAD0s6.o +NOTEPAD.sl/NOTEPAD0s5.o +NOTEPAD.sl/NOTEPAD0s4.o +NOTEPAD.sl/NOTEPAD0s3.o +NOTEPAD.sl/NOTEPAD0s2.o +NOTEPAD.sl/NOTEPAD0s1.o
if [ $? != 0 ]; then DoExitLink ; fi
IFS=$OFS
echo Linking NOTEPAD.exe
OFS=$IFS
IFS="
"
/tmp/watcom/binl64/wlink  @link26.res
if [ $? != 0 ]; then DoExitLink NOTEPAD.exe; fi
IFS=$OFS

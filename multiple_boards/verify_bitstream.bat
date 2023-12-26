@echo off
rem Wrapper script to call verify_bitstream.tcl using the Vivado lab tools under Windows

rem Get the absolute path of this script (has ending backslash)
set batdir=%~dp0

set vivado_version=2021.1
set vivado_lab_exe=C:\Xilinx\Vitis\%vivado_version%\bin\vivado_lab
if not exist "%vivado_lab_exe%" (
   echo Error: %vivado_lab_exe% doesn't exist
   goto exit
)

rem Parse command line arguments.
rem Simply checks that the supplied bitstream filename exists; doesn't verify that an actual bitstream file.
if "%~1"=="" (
   echo Usage: %0 ^<bitstream_filename^>
   goto exit
)

set bitstream_filename="%1"
if not exist "%bitstream_filename%" (
   echo Error: %bitstream_filename% doesn't exist
   goto exit
)

rem Check the mask file exists, with the same base name as the bitstream
for /f %%A in ("%bitstream_filename%") do set mask_filename="%%~dpnA.msk"
if not exist "%mask_filename%" (
   echo Error: %mask_filename% doesn't exist
   goto exit
)

rem Call the Vivado lab tool to run the TCL script to perform the verification of the supplied bitstream filename.
rem The lab tool arguments are set to disable the writing of log and journal files.
rem
rem -notrace suppresses the echoing of the TCL commands. 
rem Not shown in the vivado_lab help, but found in https://support.xilinx.com/s/article/46102?language=en_US
"%vivado_lab_exe%" -notrace -nolog -nojournal -mode batch -source "%batdir%verify_bitstream.tcl" -tclargs "%bitstream_filename%" "%mask_filename%"

:exit
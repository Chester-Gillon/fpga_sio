@echo off
rem Wrapper script to call program_bitstream.tcl using the Vivado lab tools under Windows
setlocal enabledelayedexpansion

rem Get the absolute path of this script (has ending backslash)
set batdir=%~dp0

for %%V in (2024.2 2021.1) do (
   set vivado_lab_exe=C:\Xilinx\Vitis\%%V\bin\vivado_lab
   if exist !vivado_lab_exe! (
      goto parse_args
   )
)
echo Error: Unable to find a Vivado Lab Tools installation
goto exit

rem Parse command line arguments.
rem Simply checks that the supplied bitstream filename exists; doesn't verify that an actual bitstream file.
:parse_args
if "%~1"=="" (
   echo Usage: %0 ^<bitstream_filename^>
   goto exit
)

set bitstream_filename="%1"
if not exist "%bitstream_filename%" (
   echo Error: %bitstream_filename% doesn't exist
   goto exit
)

rem Call the Vivado lab tool to run the TCL script to perform the programming of the supplied bitstream filename.
rem The lab tool arguments are set to disable the writing of log and journal files.
rem
rem -notrace suppresses the echoing of the TCL commands. 
rem Not shown in the vivado_lab help, but found in https://support.xilinx.com/s/article/46102?language=en_US
"!vivado_lab_exe!" -notrace -nolog -nojournal -mode batch -source "%batdir%program_bitstream.tcl" -tclargs "%bitstream_filename%"

:exit
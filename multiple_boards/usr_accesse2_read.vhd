----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 11/04/2023 06:43:25 PM
-- Design Name: 
-- Module Name: usr_accesse2_read - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description:
--  Allow the USR_ACCESSE2 primative to drive the input to a AXI GPIO so the value of
--  USR_ACCESSE2 of the loaded FPGA be read by software.
-- 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
library UNISIM;
use UNISIM.VComponents.all;

entity usr_accesse2_read is
    Port ( DATA_OUT : out STD_LOGIC_VECTOR (31 downto 0));
end usr_accesse2_read;

architecture Behavioral of usr_accesse2_read is

begin

USR_ACCESSE2_inst : USR_ACCESSE2
port map (
    CFGCLK => open,
    DATA => DATA_OUT,
    DATAVALID => open);

end Behavioral;

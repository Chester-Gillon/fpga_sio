----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 12/25/2024 04:29:18 PM
-- Design Name: 
-- Module Name: axi_stream_source_fixed_data - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
--  Implements a AXI stream source, which always asserts that valid data is available.
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
--  Created when testing the bandwidth which can be achieved by the DMA Bridge Subsystem for PCIe.
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity axi_stream_source_fixed_data is
    generic (TDATA_NUM_BYTES : integer);
    Port ( tdata : out STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           tkeep : out STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           tlast : out STD_LOGIC;
           tvalid : out STD_LOGIC;
           tready : in STD_LOGIC;
           s_axi_aclk : in STD_LOGIC);
end axi_stream_source_fixed_data;

architecture Behavioral of axi_stream_source_fixed_data is
-- While the s_axi_aclk isn't used, had to add it to prevent an error from the IP integrator
-- when trying to add this RTL as a module.
ATTRIBUTE X_INTERFACE_INFO : STRING;
ATTRIBUTE X_INTERFACE_INFO of s_axi_aclk: SIGNAL is "xilinx.com:signal:clock:1.0 s_axi_aclk CLK";

ATTRIBUTE X_INTERFACE_PARAMETER : STRING;
ATTRIBUTE X_INTERFACE_PARAMETER of s_axi_aclk: SIGNAL is "ASSOCIATED_BUSIF interface_axis";

begin
    tdata <= (others => '0');
    tkeep <= (others => '1');
    tlast <= '0';
    tvalid <= '1';

end Behavioral;

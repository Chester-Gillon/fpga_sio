----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 12/25/2024 11:07:52 AM
-- Design Name: 
-- Module Name: axi_stream_sink - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
--  Implements a AXI stream sink, which just asserts tready to discard all data
--  sent on the stream. 
-- Dependencies: 
-- 
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
--  Created when testing the bandwidth which can be achieved by the DMA Bridge Subsystem for PCIe.
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity axi_stream_sink is
    generic (TDATA_NUM_BYTES : integer);
    Port ( tdata : in STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           tkeep : in STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           tlast : in STD_LOGIC;
           tready : out STD_LOGIC;
           tvalid : in STD_LOGIC;
           s_axi_aclk : in STD_LOGIC);
end axi_stream_sink;

architecture Behavioral of axi_stream_sink is
-- While the s_axi_aclk isn't used, had to add it to prevent an error from the IP integrator
-- when trying to add this RTL as a module.
ATTRIBUTE X_INTERFACE_INFO : STRING;
ATTRIBUTE X_INTERFACE_INFO of s_axi_aclk: SIGNAL is "xilinx.com:signal:clock:1.0 s_axi_aclk CLK";

ATTRIBUTE X_INTERFACE_PARAMETER : STRING;
ATTRIBUTE X_INTERFACE_PARAMETER of s_axi_aclk: SIGNAL is "ASSOCIATED_BUSIF interface_axis";

begin
    tready <= '1';

end Behavioral;

----------------------------------------------------------------------------------
-- Company: 
-- Engineer: Chester Gillon
-- 
-- Create Date: 06/16/2025 03:34:43 PM
-- Design Name: 
-- Module Name: stream_crc - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
--  A 64-bit AXI stream which always operates on 8 bytes at a time, and produces a single
--  8 byte CRC packet across the each input packet.
-- Dependencies: 
--  crc_imp.vhd which was generated from http://outputlogic.com/?page_id=321
--  selecting the CRC-64-ECMA polynomial on https://en.wikipedia.org/wiki/Cyclic_redundancy_check
--
--  A block diagram is used to adapt the 8 byte wide interface of this component to a 32 byte wide
--  interface for the DMA Bridge.
-- Revision:
-- Revision 0.01 - File Created
-- Additional Comments:
-- 
----------------------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
library crc_imp;
use crc_imp.all;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity stream_crc is
    Port ( aclk : in STD_LOGIC;
           aresetn : in STD_LOGIC;
           s_tdata : in STD_LOGIC_VECTOR (63 downto 0);
           s_tlast : in STD_LOGIC;
           s_tready : out STD_LOGIC;
           s_tvalid : in STD_LOGIC;
           m_tdata : out STD_LOGIC_VECTOR (63 downto 0);
           m_tlast : out STD_LOGIC;
           m_tready : in STD_LOGIC;
           m_tvalid : out STD_LOGIC;
           m_tkeep : out STD_LOGIC_VECTOR (7 downto 0));
end stream_crc;

architecture Behavioral of stream_crc is

    signal crc_rst : std_logic;
    signal crc_en  : std_logic;
    signal crc_out : std_logic_vector (63 downto 0);
begin
    -- Update the CRC when the input data is valid
    crc_en <= m_tready and s_tvalid;

    crc_sub: entity work.crc
        port map (
        data_in => s_tdata,
        crc_en => crc_en,
        rst => crc_rst,
        clk => aclk,
        crc_out => crc_out
        );    

    -- Don't accept input unless the output is ready
    s_tready <= m_tready;

    -- The output is always only a single word.
    m_tlast <= '1';
    
    -- Pass out the current CRC
    m_tdata <= crc_out;
    
    -- Output has a tkeep since the external 8-to-32 byte width convertor needs to preserve the 8 byte
    -- packet size.
    m_tkeep <= (others => '1');
    
    process (aclk) begin
        if (aclk'EVENT and aclk = '1') then
            -- Reset the CRC sequence on AXI reset or end of the input stream
            crc_rst <= (not aresetn) or s_tlast;

            -- Output the CRC at the end of the input stream.
            m_tvalid <= s_tlast;
        end if;
    end process;

end Behavioral;

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
--  A 256-bit AXI stream which always operates on 32 bytes at a time, and produces a single
--  8 byte CRC packet across the each input packet.
-- Dependencies: 
--  crc_imp.vhd which was generated from https://git.bues.ch/git/crcgen.git using:
--    ./crcgen -V -a CRC-64-ECMA -b 256 -D data_in -C crc_in -o crc_out
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
           s_tdata : in STD_LOGIC_VECTOR (255 downto 0);
           s_tlast : in STD_LOGIC;
           s_tready : out STD_LOGIC;
           s_tvalid : in STD_LOGIC;
           m_tdata : out STD_LOGIC_VECTOR (255 downto 0);
           m_tlast : out STD_LOGIC;
           m_tready : in STD_LOGIC;
           m_tvalid : out STD_LOGIC;
           m_tkeep : out STD_LOGIC_VECTOR (31 downto 0));
end stream_crc;

architecture Behavioral of stream_crc is

    signal crc_rst      : std_logic;
    signal crc_en       : std_logic;
    signal previous_crc : std_logic_vector (63 downto 0);
    signal new_crc      : std_logic_vector (63 downto 0);
begin
    -- Update the CRC when the input data is valid
    crc_en <= m_tready and s_tvalid;

    crc_sub: entity work.crc
        port map (
        data_in => s_tdata,
        crc_in => previous_crc,
        crc_out => new_crc
        );    

    -- Don't accept input if either:
    -- a. The output isn't ready.
    -- b. The CRC sequence is being reset.
    s_tready <= m_tready and (not crc_rst);

    -- The output is always only a single word.
    m_tlast <= '1';
    
    -- Pass out the current 64-bit CRC on the least significant bits 
    m_tdata(63 downto 0) <= previous_crc;
    m_tdata(255 downto 64) <= (others => '0');
    
    -- Indictate only a 8 byte packet output size.
    m_tkeep(7 downto 0) <= (others => '1');
    m_tkeep(31 downto 8) <= (others => '0');
    
    process (aclk) begin
        if (aclk'EVENT and aclk = '1') then
            -- Reset the CRC sequence on AXI reset or end of the input stream
            crc_rst <= (not aresetn) or s_tlast;
            
            if (crc_rst = '1') then
                previous_crc <= (others => '1');
            elsif (crc_en = '1') then
                previous_crc <= new_crc;
            end if; 

            -- Output the CRC at the end of the input stream.
            m_tvalid <= crc_en and s_tlast;
        end if;
    end process;

end Behavioral;

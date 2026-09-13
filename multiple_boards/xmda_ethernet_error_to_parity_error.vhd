----------------------------------------------------------------------------------
-- Company: 
-- Engineer: Chester Gillon
-- 
-- Create Date: 09/12/2026 10:32:03 AM
-- Design Name: 
-- Module Name: xmda_ethernet_error_to_parity_error - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: VHDL 2008
-- Description: 
--   Interface between a AXI4 Stream from a CMAC receive port to a XMDA C2H port, in order to be able to
--   indicate a receive packet error to the XDMA C2H port.
--
--   The CMAC receive has a single tuser bit which indicates a receive packet error.
--
--   The XMDA is configured to "Propagate Parity", and this module indicates a parity error to the
--   XDMA when the CMAC indicates a receive packet error.
--
--   For simplicity this module is entirely combitorial.
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
--library UNISIM;
--use UNISIM.VComponents.all;

entity xmda_ethernet_error_to_parity_error is
    generic (TDATA_NUM_BYTES : integer);
    Port ( s_axi_aclk : in STD_LOGIC;
           s_tdata : in STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           s_tlast : in STD_LOGIC;
           s_tready : out STD_LOGIC;
           s_tvalid : in STD_LOGIC;
           s_tkeep : in STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           s_tuser : in STD_LOGIC;
           m_tdata : out STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           m_tlast : out STD_LOGIC;
           m_tready : in STD_LOGIC;
           m_tvalid : out STD_LOGIC;
           m_tkeep : out STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           m_tuser : out STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0));
end xmda_ethernet_error_to_parity_error;

architecture Behavioral of xmda_ethernet_error_to_parity_error is
    -- While the s_axi_aclk isn't used, had to add it to prevent an error from the IP integrator
    -- when trying to add this RTL as a module.

    -- Declare attributes for clocks. See UG994
    ATTRIBUTE X_INTERFACE_INFO : STRING;
    ATTRIBUTE X_INTERFACE_INFO of s_axi_aclk: SIGNAL is "xilinx.com:signal:clock:1.0 s_axi_aclk CLK";
    ATTRIBUTE X_INTERFACE_PARAMETER : STRING;
    ATTRIBUTE X_INTERFACE_PARAMETER of s_axi_aclk: SIGNAL is "ASSOCIATED_BUSIF s:m";

begin
    -- Pass through the non-tuser signals with no modification
    s_tready <= m_tready;
    
    m_tdata <= s_tdata;
    m_tlast <= s_tlast;
    m_tvalid <= s_tvalid;
    m_tkeep <= s_tkeep;

    generate_parity : process (s_tdata, s_tvalid, s_tlast, s_tuser)
        variable odd_parity : STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
        variable i : integer;
    begin
        -- Calculate the odd parity for each data byte
        for i in odd_parity'range loop
            odd_parity(i) := not (xor (s_tdata((((i + 1) * 8)) - 1 downto (i * 8))));
        end loop;

        for i in odd_parity'range loop
            if s_tvalid and s_tlast and s_tuser then
                -- The CMAC has indicated a receive error on the last transfer, so indicate a parity
                -- failure to the XMDA to propogate the error indication.
                m_tuser(i) <= not odd_parity(i);
            else
                -- Otherwise indicate valid parity.
                m_tuser(i) <= odd_parity(i);
            end if;
        end loop;
    end process;
end Behavioral;

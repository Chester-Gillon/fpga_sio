----------------------------------------------------------------------------------
-- Company: 
-- Engineer: Chester Gillon 
-- 
-- Create Date: 09/12/2026 04:10:04 PM
-- Design Name: 
-- Module Name: xmda_parity_error_to_ethernet_error - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: VHDL 2008
-- Description: 
--   Interface between a XDMA H2C port and a CMAC transmit port in order to generate a transmit error
--   if the parity check fails on the data from the XDMA.
--
--   This is intended to support use of the XMDA configured with "Propagate Parity" enabled.
--   It sets the m_tuser packet transmit error to the CMAC if the parity checked failed for any byte
--   in the data of a packet.
--
--   Unsure of how likely a parity error from the XMDA is, but this module was added to support
--   use of the xmda_ethernet_error_to_parity_error module, which is used for the reverse direction
--   to indicate a parity error to the XMDA when a receive packet from the CMAC has an error.   
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

entity xmda_parity_error_to_ethernet_error is
    generic (TDATA_NUM_BYTES : integer);
    Port ( s_axi_aclk : in STD_LOGIC;
           aresetn : in STD_LOGIC;
           s_tdata : in STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           s_tlast : in STD_LOGIC;
           s_tready : out STD_LOGIC;
           s_tvalid : in STD_LOGIC;
           s_tkeep : in STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           s_tuser : in STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           m_tdata : out STD_LOGIC_VECTOR ((TDATA_NUM_BYTES * 8) - 1 downto 0);
           m_tlast : out STD_LOGIC;
           m_tready : in STD_LOGIC;
           m_tvalid : out STD_LOGIC;
           m_tkeep : out STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
           m_tuser : out STD_LOGIC);
end xmda_parity_error_to_ethernet_error;

architecture Behavioral of xmda_parity_error_to_ethernet_error is
    -- Declare attributes for clocks and resets. See UG994
    ATTRIBUTE X_INTERFACE_INFO : STRING;
    ATTRIBUTE X_INTERFACE_INFO of s_axi_aclk: SIGNAL is "xilinx.com:signal:clock:1.0 s_axi_aclk CLK";
    ATTRIBUTE X_INTERFACE_PARAMETER : STRING;
    ATTRIBUTE X_INTERFACE_PARAMETER of s_axi_aclk: SIGNAL is "ASSOCIATED_RESET aresetn, ASSOCIATED_BUSIF s:m";
    
    ATTRIBUTE X_INTERFACE_INFO of aresetn: SIGNAL is "xilinx.com:signal:reset:1.0 aresetn RST";
    ATTRIBUTE X_INTERFACE_PARAMETER of aresetn: SIGNAL is "POLARITY ACTIVE_LOW";

    signal parity_error : STD_LOGIC;
    signal latched_parity_error : STD_LOGIC;

begin
    -- Pass through the non-tuser signals with no modification
    s_tready <= m_tready;
    
    m_tdata <= s_tdata;
    m_tlast <= s_tlast;
    m_tvalid <= s_tvalid;
    m_tkeep <= s_tkeep;


    check_parity : process (s_tdata, s_tvalid, s_tlast, s_tkeep, s_tuser, latched_parity_error, parity_error)
        variable odd_parity : STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
        variable parity_errors : STD_LOGIC_VECTOR (TDATA_NUM_BYTES - 1 downto 0);
        variable i : integer;
    begin
        -- Calculate the odd parity for each data byte
        for i in odd_parity'range loop
            odd_parity(i) := not (xor (s_tdata((((i + 1) * 8)) - 1 downto (i * 8))));
        end loop;

        -- Calculate a parity error indication for each data byte
        for i in odd_parity'range loop
            -- Default to no parity error unless the data byte is valid
            parity_errors(i) := '0';
            
            if s_tvalid then
                if s_tlast then
                    -- On the final transfer only check parity on the used bytes
                    if s_tkeep(i) then
                        if s_tuser(i) /= odd_parity(i) then
                            parity_errors(i) := '1';
                        end if;
                    end if;
                else
                    -- On other than the final transfer check parity on all bytes
                    if s_tuser(i) /= odd_parity(i) then
                        parity_errors(i) := '1';
                    end if;
                end if;
            end if;
        end loop;

        -- Determine if a parity error on any byte        
        parity_error <= or parity_errors;

        if s_tvalid and s_tlast then
            -- On the final transfer indicate if a parity error was detected on any byte of the packet
            m_tuser <= latched_parity_error or parity_error;
        else
            m_tuser <= '0';
        end if;
    end process;

    -- Maintain a latched parity error for the current packet    
    latch_parity_error : process (s_axi_aclk)
    begin
        if (s_axi_aclk'EVENT and s_axi_aclk = '1') then
            if (aresetn = '0') then
                -- External reset
                latched_parity_error <= '0';
            elsif s_tvalid and s_tlast then
                -- Reset at end of packet
                latched_parity_error <= '0';
            elsif parity_error then
                -- Latch parity error during packet
                latched_parity_error <= '1';
            end if;
        end if;
    end process;

end Behavioral;

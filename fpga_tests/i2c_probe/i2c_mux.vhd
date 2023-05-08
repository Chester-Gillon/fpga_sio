----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date: 05/07/2023 11:37:25 AM
-- Design Name: 
-- Module Name: i2c_mux - Behavioral
-- Project Name: 
-- Target Devices: 
-- Tool Versions: 
-- Description: 
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
--library UNISIM;
--use UNISIM.VComponents.all;

entity i2c_mux is
Port ( 
    -- The connections to AXI IIC peripheral
    sda_i      : out std_logic;
    sda_o      : in std_logic;
    sda_t      : in std_logic;
    scl_i      : out std_logic;
    scl_o      : in std_logic;
    scl_t      : in std_logic;

    -- The connections to the AXI GPIO which allows a selection of either connecting the 
    -- CPLD interface to the AXI IIC peripheral or a bit-banged I2C from the GPIO signals 
    gpio_io_i  : out std_logic_vector(3 downto 0);
    gpio_io_o  : in std_logic_vector(3 downto 0);
    gpio_io_t  : in std_logic_vector(3 downto 0);
    
    -- Connections to the CPLD on the Trenz Electronic TEF1001
    CPLD_1_SCL  :  out std_logic;
    CPLD_14_OE  :  out std_logic;
    CPLD_16_SDA :  in std_logic
    );
end i2c_mux;

architecture Behavioral of i2c_mux is

-- Allow the Block Diagram to automatically recognise this as a IIC bus
ATTRIBUTE X_INTERFACE_INFO:              STRING;
ATTRIBUTE X_INTERFACE_INFO of sda_i: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC sda_i";
ATTRIBUTE X_INTERFACE_INFO of sda_o: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC sda_o";
ATTRIBUTE X_INTERFACE_INFO of sda_t: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC sda_t";
ATTRIBUTE X_INTERFACE_INFO of scl_i: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC scl_i";
ATTRIBUTE X_INTERFACE_INFO of scl_o: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC scl_o";
ATTRIBUTE X_INTERFACE_INFO of scl_t: SIGNAL is "xilinx.com:interface:iic_rtl:1.0 IIC scl_t";

signal select_bit_bang: std_logic;
signal bit_banged_scl: std_logic;
signal bit_banged_sda_out: std_logic;
signal bit_banged_sda_in: std_logic;
signal cpld_scl: std_logic;

begin

    -- The GPIO output which is used to select either the AXI IIC or the bit-banged GPIO 
    -- as connected to the CPLD.
    select_bit_bang <= gpio_io_o(3);

    -- The SDA value from the I2C bus is always passed to the AXI IIC and GPIO regardless of 
    -- the mux selection.
    sda_i <= CPLD_16_SDA;
    bit_banged_sda_out <= CPLD_16_SDA;
    gpio_io_i(0) <= bit_banged_sda_out;

    -- Mux the SCL and SDA (OE) outputs to the CPLD.
    bit_banged_sda_in <= gpio_io_o(1);
    bit_banged_scl <= gpio_io_o(2);

    cpld_scl <= bit_banged_scl when (select_bit_bang='1') else (scl_o or scl_t);
    CPLD_1_SCL <= cpld_scl;
    CPLD_14_OE <= bit_banged_sda_in when (select_bit_bang='1') else (sda_o or sda_t);

    -- The CPLD doesn't allow the actual SCL signal on the I2C bus to be monitored, so simply 
    -- feedback the CPLD SCL output to AXI IIC input.
    --
    -- This should allow the AXI IIC to track the bus-busy state when the GPIO bit-banged controller 
    -- is selected.
    scl_i <= cpld_scl;
end Behavioral;

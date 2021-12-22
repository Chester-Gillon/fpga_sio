library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity SCF1001 is port ( 
  --
  -- External I2C
  --
  ext_sda_i      : in  std_logic;
  ext_sda_o      : out std_logic;
  ext_sda_t      : out std_logic;
  ext_scl_i      : in  std_logic;
  ext_scl_o      : out std_logic;
  ext_scl_t      : out std_logic;


	--	
	CPLD_1_SCL  :  out std_logic; --
	CPLD_14_OE  :  out std_logic; --
	CPLD_16_SDA :  in std_logic; --
    --
    -- Connect to EMIO I2C1
    --
	sda_i      : out  std_logic;
	sda_o      : in std_logic;
	sda_t      : in std_logic;
	scl_i      : out  std_logic;
	scl_o      : in std_logic;
	scl_t      : in std_logic
	);
end SCF1001;

architecture Behavioral of SCF1001 is

signal sda: std_logic;
signal scl: std_logic;

begin
	
	-- I2C bus merger
	ext_sda_o <= sda_o;
	ext_sda_t <= sda_t;
	ext_scl_t <= scl_t;	
	
	
	-- SDA readback from SC to I2C core
	sda_i 	<= CPLD_16_SDA and ext_sda_i;
	-- SDA/SCL pass through to SC	
	CPLD_14_OE <= sda;
	CPLD_1_SCL <= scl;
	-- internal signals
	sda 	<= sda_o or sda_t;
	scl 	<= scl_o or scl_t;
	-- SCL feedback to I2C core
	scl_i 	<= scl;
	--
	
end Behavioral;
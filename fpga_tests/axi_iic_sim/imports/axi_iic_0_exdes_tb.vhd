


library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_misc.all;
use ieee.std_logic_textio.all;

library std;
use std.textio.all;
library unisim;
use unisim.vcomponents.all;



entity axi_iic_0_exdes_tb is

end entity;

architecture tb of axi_iic_0_exdes_tb is

component axi_iic_0_exdes 
   port (
         clk_in1_p : in std_logic;
         clk_in1_n : in std_logic;
         reset : in std_logic;
         start : in std_logic;
         scl_io : inout std_logic;
         sda_io : inout std_logic;
         to_led : out std_logic_vector (1-1 downto 0);
         done : out std_logic);
end component;

constant clk_per : time := 5 ns;
signal clock : std_logic := '0';
signal clock_n : std_logic := '1';
signal reset : std_logic := '1';
signal start : std_logic := '0';
signal status : std_logic := '0';
signal test : std_logic := '0';
signal done : std_logic := '0';
signal led : std_logic_vector (1-1 downto 0);
signal all_zero : std_logic_vector (1-1 downto 0);

signal scl_io, sda_io : std_logic;
signal scl_o, scl_i, scl_tri : std_logic;
signal sda_o, sda_i : std_logic; 
signal sda_tri : std_logic := '1';

signal start_det, stop_det : std_logic;
signal address_det, add_ack : std_logic := '0';
signal address : std_logic_vector (7 downto 0);
signal address2 : std_logic_vector (7 downto 0);

signal command_det, command_ack : std_logic := '0';
signal command : std_logic_vector (7 downto 0);

signal rd_addr_det, rd_addr_ack : std_logic := '0';
signal rd_addr : std_logic_vector (7 downto 0);
signal rd_addr2 : std_logic_vector (7 downto 0);

signal repeat_start : std_logic := '0';

signal read_data : std_logic_vector (7 downto 0) := "01010101";
signal read_data2 : std_logic_vector (7 downto 0) := "10101011";

signal first_byte : std_logic := '0';
signal second_byte : std_logic := '0';
signal first_ack_from_i2c, second_ack_from_i2c : std_logic := '1';
signal tb_stop : std_logic := '0';
signal a,b : std_logic := '0';
signal a_ack, b_ack : std_logic := '0';
signal scl_int : std_logic := '0';


begin

all_zero <= (others => '0');
scl_tri <= '1';


process
begin
    wait for (clk_per/2);
    clock <= not clock;
    clock_n <= not clock_n;

end process;


 reset <= '0' after 100 ns;

  

start <= '1' after 100 ns;


DUT_TB: axi_iic_0_exdes
      port map (
      clk_in1_p => clock,
      clk_in1_n => clock_n,
      reset => reset,
      start => start,
      done => done,
      scl_io => scl_io,
      sda_io => sda_io,
      to_led => led
      );

     scl_inst : IOBUF
       port map (
         IO         => scl_io,
         I          => scl_o,
         O          => scl_int,
         T          => scl_tri);

     sda_inst : IOBUF
       port map (
         IO         => sda_io,
         I          => sda_o,
         O          => sda_i,
         T          => sda_tri);

   
   scl_pullup : pullup PORT MAP (
      O     => scl_io
   );

   sda_pullup : pullup PORT MAP (
      O     => sda_io
   );

   scl_i <= transport scl_int after 401 ns; 

----------------
process (sda_i)
begin
      if (sda_i'event and sda_i = '0') then
         if (scl_i = '1' and command_det = '1') then
            repeat_start <= '1';
--            report "Repeated Start condition detected" severity note;
         elsif (scl_i = '1') then
            start_det <= '1';
--            report "Start condition detected" severity note;
         end if;
      end if;
end process; 


process (sda_i)
begin
      if (sda_i'event and sda_i = '1') then
         if (scl_i = '1') then
            stop_det <= '1';
--            report "Stop condition detected" severity note;
         end if;
      end if;
end process; 


process (scl_i)
begin
     if (scl_i'event and scl_i = '1') then
        if (start_det = '1' and address_det = '0') then
          address (0) <= sda_i;
          address (7 downto 1)  <= address (6 downto 0);
        elsif (add_ack = '1' and command_det = '0') then
          command (0) <= sda_i;         
          command (7 downto 1)  <= command (6 downto 0);
        elsif (command_ack = '1' and rd_addr_det = '0') then
          rd_addr (0) <= sda_i;
          rd_addr (7 downto 1) <= rd_addr (6 downto 0); 
        elsif (first_byte = '1' and sda_tri = '1' and second_byte = '0') then
          first_ack_from_i2c <= sda_i;
          report "First ACK received from I2C" severity note;
        elsif (first_byte = '1' and sda_tri = '1' and second_byte = '1' and tb_stop = '0') then
          tb_stop <= '1';
          second_ack_from_i2c <= sda_i;    
          report "Second ACK received from I2C" severity note;
        end if;
     end if;
end process; 
address_det <= '1' when (address = "01101000") else '0';
rd_addr_det <= '1' when (rd_addr = "01101001") else '0';
command_det <= '1' when (command = "00100001") else '0';

first_byte <= '1' when (read_data = "00000000") else '0';
second_byte <= '1' when (read_data2 = "00000000") else '0';

process (scl_i)
begin
     if (scl_i'event and scl_i = '0') then
        if (address_det = '1' and add_ack = '0') then
            sda_tri <= '0';
            sda_o <= '0';
            add_ack <= '1'; 
        elsif (command_det = '1' and command_ack = '0') then
            sda_tri <= '0';
            sda_o <= '0';
            command_ack <= '1';
        elsif (rd_addr_det = '1' and rd_addr_ack = '0') then
            sda_tri <= '0';
            sda_o <= '0';
            rd_addr_ack <= '1';     
        elsif (rd_addr_ack = '1' and first_byte = '0') then
            sda_tri <= '0';
            sda_o <= read_data (7);
            read_data (7 downto 1) <= read_data (6 downto 0);
            read_data (0) <= '0';
        elsif (first_ack_from_i2c = '0' and second_byte = '0') then
            sda_tri <= '0';
            sda_o <= read_data2 (7);
            read_data2 (7 downto 1) <= read_data2 (6 downto 0);
            read_data2 (0) <= '0'; 
   
        else 
            sda_tri <= '1';
        end if;
      end if;
end process;             



process (done)
    procedure simtimeprint is
      variable outline : line;
    begin
      write(outline, string'("## SYSTEM_CYCLE_COUNTER "));
      write(outline, NOW/clk_per);
      write(outline, string'(" ns"));
      writeline(output,outline);
    end simtimeprint;
begin

if (done = '1' and led /= all_zero) then
 simtimeprint;
 report "Test Completed Successfully" severity failure;
elsif (status = '0' and done = '1') then
 simtimeprint;
 report "Test Failed !!!" severity failure;
end if;
end process;

process
begin
     wait for 100000000 ns;
     report "Test Failed !! Test Timed Out" severity failure;
end process;

end tb;


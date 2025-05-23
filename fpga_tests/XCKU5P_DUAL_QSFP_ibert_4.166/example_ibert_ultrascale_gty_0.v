






// file: ibert_ultrascale_gty_0.v
//////////////////////////////////////////////////////////////////////////////

//
// Module example_ibert_ultrascale_gty_0

//////////////////////////////////////////////////////////////////////////////





`define C_NUM_GTY_QUADS 2
`define C_GTY_TOTAL_CH 8
`define C_GTY_REFCLKS_USED 1
module example_ibert_ultrascale_gty_0
(
  // GT top level ports
  output [(`C_GTY_TOTAL_CH)-1:0]		gty_txn_o,
  output [(`C_GTY_TOTAL_CH)-1:0]		gty_txp_o,
  input  [(`C_GTY_TOTAL_CH)-1:0]    	gty_rxn_i,
  input  [(`C_GTY_TOTAL_CH)-1:0]   	gty_rxp_i,
  input                           	gty_sysclkp_i,
  input  [`C_GTY_REFCLKS_USED-1:0]      gty_refclk0p_i,
  input  [`C_GTY_REFCLKS_USED-1:0]      gty_refclk0n_i,
  input  [`C_GTY_REFCLKS_USED-1:0]      gty_refclk1p_i,
  input  [`C_GTY_REFCLKS_USED-1:0]      gty_refclk1n_i
);

  //
  // Ibert refclk internal signals
  //
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk0_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk1_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk0_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk1_i;        	
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk0_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk1_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk00_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk10_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk01_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qrefclk11_i;  
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk00_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk10_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk01_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qnorthrefclk11_i;  
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk00_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk10_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk01_i;
   wire  [`C_NUM_GTY_QUADS-1:0]    gty_qsouthrefclk11_i; 
   wire  [`C_GTY_REFCLKS_USED-1:0] gty_refclk0_i;
   wire  [`C_GTY_REFCLKS_USED-1:0] gty_refclk1_i;
   wire  [`C_GTY_REFCLKS_USED-1:0] gty_odiv2_0_i;
   wire  [`C_GTY_REFCLKS_USED-1:0] gty_odiv2_1_i;
   wire                        gty_sysclk_i;

  //
  // Refclk IBUFDS instantiations
  //







 
    IBUFDS_GTE4 u_buf_q3_clk0
      (
        .O            (gty_refclk0_i[0]),
        .ODIV2        (gty_odiv2_0_i[0]),
        .CEB          (1'b0),
        .I            (gty_refclk0p_i[0]),
        .IB           (gty_refclk0n_i[0])
      );

    IBUFDS_GTE4 u_buf_q3_clk1
      (
        .O            (gty_refclk1_i[0]),
        .ODIV2        (gty_odiv2_1_i[0]),
        .CEB          (1'b0),
        .I            (gty_refclk1p_i[0]),
        .IB           (gty_refclk1n_i[0])
      );


  //
  // Refclk connection from each IBUFDS to respective quads depending on the source selected in gui
  //





  assign gty_qrefclk0_i[0] = 1'b0;
  assign gty_qrefclk1_i[0] = 1'b0;
  assign gty_qnorthrefclk0_i[0] = 1'b0;
  assign gty_qnorthrefclk1_i[0] = 1'b0;
  assign gty_qsouthrefclk0_i[0] = 1'b0;
  assign gty_qsouthrefclk1_i[0] = gty_refclk1_i[0];
//GTYE4_COMMON clock connection
  assign gty_qrefclk00_i[0] = 1'b0;
  assign gty_qrefclk10_i[0] = 1'b0;
  assign gty_qrefclk01_i[0] = 1'b0;
  assign gty_qrefclk11_i[0] = 1'b0;
  assign gty_qnorthrefclk00_i[0] = 1'b0;
  assign gty_qnorthrefclk10_i[0] = 1'b0;
  assign gty_qnorthrefclk01_i[0] = 1'b0;
  assign gty_qnorthrefclk11_i[0] = 1'b0;
  assign gty_qsouthrefclk00_i[0] = 1'b0;
  assign gty_qsouthrefclk10_i[0] = 1'b0;
  assign gty_qsouthrefclk01_i[0] = 1'b0;
  assign gty_qsouthrefclk11_i[0] = gty_refclk1_i[0];
 
  assign gty_qrefclk0_i[1] = gty_refclk0_i[0];
  assign gty_qrefclk1_i[1] = gty_refclk1_i[0];
  assign gty_qnorthrefclk0_i[1] = 1'b0;
  assign gty_qnorthrefclk1_i[1] = 1'b0;
  assign gty_qsouthrefclk0_i[1] = 1'b0;
  assign gty_qsouthrefclk1_i[1] = 1'b0;
//GTYE4_COMMON clock connection
  assign gty_qrefclk00_i[1] = 1'b0;
  assign gty_qrefclk10_i[1] = 1'b0;
  assign gty_qrefclk01_i[1] = gty_refclk0_i[0];
  assign gty_qrefclk11_i[1] = gty_refclk1_i[0];  
  assign gty_qnorthrefclk00_i[1] = 1'b0;
  assign gty_qnorthrefclk10_i[1] = 1'b0;
  assign gty_qnorthrefclk01_i[1] = 1'b0;
  assign gty_qnorthrefclk11_i[1] = 1'b0;  
  assign gty_qsouthrefclk00_i[1] = 1'b0;
  assign gty_qsouthrefclk10_i[1] = 1'b0;  
  assign gty_qsouthrefclk01_i[1] = 1'b0;
  assign gty_qsouthrefclk11_i[1] = 1'b0;

  //
  // Sysclock connection
  //
  assign gty_sysclk_i = gty_sysclkp_i;

  //
  // IBERT core instantiation
  //
  ibert_ultrascale_gty_0 u_ibert_gty_core
    (
      .txn_o(gty_txn_o),
      .txp_o(gty_txp_o),
      .rxn_i(gty_rxn_i),
      .rxp_i(gty_rxp_i),
      .clk(gty_sysclk_i),
      .gtrefclk0_i(gty_qrefclk0_i),
      .gtrefclk1_i(gty_qrefclk1_i),
      .gtnorthrefclk0_i(gty_qnorthrefclk0_i),
      .gtnorthrefclk1_i(gty_qnorthrefclk1_i),
      .gtsouthrefclk0_i(gty_qsouthrefclk0_i),
      .gtsouthrefclk1_i(gty_qsouthrefclk1_i),
      .gtrefclk00_i(gty_qrefclk00_i),
      .gtrefclk10_i(gty_qrefclk10_i),
      .gtrefclk01_i(gty_qrefclk01_i),
      .gtrefclk11_i(gty_qrefclk11_i),
      .gtnorthrefclk00_i(gty_qnorthrefclk00_i),
      .gtnorthrefclk10_i(gty_qnorthrefclk10_i),
      .gtnorthrefclk01_i(gty_qnorthrefclk01_i),
      .gtnorthrefclk11_i(gty_qnorthrefclk11_i),
      .gtsouthrefclk00_i(gty_qsouthrefclk00_i),
      .gtsouthrefclk10_i(gty_qsouthrefclk10_i),
      .gtsouthrefclk01_i(gty_qsouthrefclk01_i),
      .gtsouthrefclk11_i(gty_qsouthrefclk11_i)
    );

endmodule

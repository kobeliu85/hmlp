#ifndef CNN_HPP
#define CNN_HPP

#include <hmlp.h>
#include <hmlp_internal.hpp>
#include <hmlp_packing.hpp>
#include <hmlp_util.hpp>
#include <hmlp_thread.hpp>
#include <hmlp_runtime.hpp>

#define DEBUG_CONV2D 1

namespace hmlp
{
namespace cnn
{

/**
 *
 */ 
template<
  int KC, int MR, int NR, int PACK_MR, int PACK_NR,
  typename SEMIRINGKERNEL,
  typename TA, typename TB, typename TC, typename TV>
void rank_k_macro_kernel
(
  Worker &thread,
  int ic, int jc, int pc,
  int  m, int n,  int  k,
  TA *packA,
  TB *packB,
  TV *C, int ldc,
  SEMIRINGKERNEL semiringkernel
)
{
  thread_communicator &ic_comm = *thread.ic_comm;

  auto loop3rd = GetRange( 0, n,      NR, thread.jr_id, ic_comm.GetNumThreads() );
  auto pack3rd = GetRange( 0, n, PACK_NR, thread.jr_id, ic_comm.GetNumThreads() );
  auto loop2nd = GetRange( 0, m,      MR );
  auto pack2nd = GetRange( 0, m, PACK_MR );

  for ( int j   = loop3rd.beg(), jp  = pack3rd.beg(); 
            j   < loop3rd.end();
            j  += loop3rd.inc(), jp += pack3rd.inc() )     // beg 3rd loop
  {
    struct aux_s<TA, TB, TC, TV> aux;
    aux.pc       = pc;
    aux.b_next   = packB;
    aux.do_packC = 0;
    aux.jb       = std::min( n - j, NR );

    for ( int i  = loop2nd.beg(), ip  = pack2nd.beg(); 
              i  < loop2nd.end(); 
              i += loop2nd.inc(), ip += pack2nd.inc() )    // beg 2nd loop
    {
      aux.ib = std::min( m - i, MR );
      if ( aux.ib != MR ) 
      {
        aux.b_next += ic_comm.GetNumThreads() * PACK_NR * k;
      }
      
      if ( aux.jb == NR && aux.ib == MR )                 
      {
        semiringkernel
        (
          k,
          &packA[ ip * k ],
          &packB[ jp * k ],
          &C[ j * ldc + i ], ldc,
          &aux
        );
      }
      else                                                 // corner case
      {
        // TODO: this should be initC.
        TV ctmp[ MR * NR ] = { (TV)0.0 };
        semiringkernel
        (
          k,
          &packA[ ip * k ],
          &packB[ jp * k ],
          ctmp, MR,
          &aux
        );
        if ( pc )
        {
          for ( auto jj = 0; jj < aux.jb; jj ++ )
          {
            for ( auto ii = 0; ii < aux.ib; ii ++ )
            {
              C[ ( j + jj ) * ldc + i + ii ] += ctmp[ jj * MR + ii ];
            }
          }
        }
        else 
        {
          for ( auto jj = 0; jj < aux.jb; jj ++ )
          {
            for ( auto ii = 0; ii < aux.ib; ii ++ )
            {
              C[ ( j + jj ) * ldc + i + ii ] = ctmp[ jj * MR + ii ];
            }
          }
        }
      }
    }                                                      // end 2nd loop
  }                                                        // end 3rd loop
}                                                          // end rank_k_macro_kernel

/**
 *
 */ 
template<int KC, int MR, int NR, int PACK_MR, int PACK_NR,
    typename MICROKERNEL,
    typename TA, typename TB, typename TC, typename TV>
void fused_macro_kernel
(
  Worker &thread,
  int ic, int jc, int pc,
  int  m,  int n,  int k,
  TA *packA,
  TB *packB,
  TV *C, int ldc,
  MICROKERNEL microkernel
)
{
  thread_communicator &ic_comm = *thread.ic_comm;

  auto loop3rd = GetRange( 0, n,      NR, thread.jr_id, ic_comm.GetNumThreads() );
  auto pack3rd = GetRange( 0, n, PACK_NR, thread.jr_id, ic_comm.GetNumThreads() );
  auto loop2nd = GetRange( 0, m,      MR );
  auto pack2nd = GetRange( 0, m, PACK_MR );

  for ( int j   = loop3rd.beg(), jp  = pack3rd.beg(); 
            j   < loop3rd.end();
            j  += loop3rd.inc(), jp += pack3rd.inc() )     // beg 3rd loop
  {
    struct aux_s<TA, TB, TC, TV> aux;
    aux.pc       = pc;
    aux.b_next   = packB;
    aux.do_packC = 0;
    aux.jb       = std::min( n - j, NR );

    for ( int i  = loop2nd.beg(), ip  = pack2nd.beg(); 
              i  < loop2nd.end(); 
              i += loop2nd.inc(), ip += pack2nd.inc() )    // beg 2nd loop
    {
      aux.ib = std::min( m - i, MR );
      if ( aux.ib != MR ) 
      {
        aux.b_next += ic_comm.GetNumThreads() * PACK_NR * k;
      }

      if ( aux.jb == NR && aux.ib == MR )                 
      {
        microkernel
        (
          k,
          &packA[ ip * k ],
          &packB[ jp * k ],
          &C[ j * ldc + i ], ldc,
          &aux
        );
      }
      else                                                 // corner case
      {
        // TODO: this should be initC.
        TV ctmp[ MR * NR ] = { (TV)0.0 };
        microkernel
        (
          k,
          &packA[ ip * k ],
          &packB[ jp * k ],
          ctmp, MR,
          &aux
        );

        if ( pc )
        {
          for ( auto jj = 0; jj < aux.jb; jj ++ )
          {
            for ( auto ii = 0; ii < aux.ib; ii ++ )
            {
              C[ ( j + jj ) * ldc + i + ii ] += ctmp[ jj * MR + ii ];
            }
          }
        }
        else 
        {
          for ( auto jj = 0; jj < aux.jb; jj ++ )
          {
            for ( auto ii = 0; ii < aux.ib; ii ++ )
            {
              C[ ( j + jj ) * ldc + i + ii ] = ctmp[ jj * MR + ii ];
            }
          }
        }
      }
    }                                                      // end 2nd loop
  }                                                        // end 3rd loop
}                                                          // end fused_macro_kernel



/*
 *
 */ 
template<
  int MC, int NC, int KC, int MR, int NR, 
  int PACK_MC, int PACK_NC, int PACK_MR, int PACK_NR, int ALIGN_SIZE,
  bool USE_STRASSEN,
  typename SEMIRINGKERNEL, typename MICROKERNEL,
  typename TA, typename TB, typename TC, typename TV>
void conv2d_internal
(
  Worker &thread,
  int w0, int h0, int d0, int s, int p,
  TB *B, 
  int w1, int h1, int d1,
  TA *A,
  TC *C,
  SEMIRINGKERNEL semiringkernel,
  MICROKERNEL microkernel,
  int nc, int pack_nc,
  TA *packA, 
  TB *packB 
)
{
  packA  += ( thread.jc_id * thread.ic_nt                ) * PACK_MC * KC
          + ( thread.ic_id                               ) * PACK_MC * KC;
  packB  += ( thread.jc_id                               ) * pack_nc * KC;


  // Now compute parameters such that I can transform the problem into GEMM.
  int m = d1;
  int nx = ( w0 - w1 + 2 * p ) / s + 1;
  int ny = ( h0 - h1 + 2 * p ) / s + 1;
  int n = nx * ny;
  int k = w1 * h1 * d0;

  auto loop6th = GetRange( 0, n, nc, thread.jc_id, thread.jc_nt );
  auto loop5th = GetRange( 0, k, KC );
  auto loop4th = GetRange( 0, m, MC, thread.ic_id, thread.ic_nt );

  /*
   *  @CHENHAN: loop over your filters.
   */ 
  for ( int jc  = loop6th.beg(); 
            jc  < loop6th.end(); 
            jc += loop6th.inc() )                          // beg 6th loop 
  {
    auto &jc_comm = *thread.jc_comm;
    auto jb = std::min( n - jc, nc );

    /*
     *  @CHENHAN: loop over your window size ( w1 * h1 * d0 ).
     */ 
    for ( int pc  = loop5th.beg();
              pc  < loop5th.end();
              pc += loop5th.inc() )
    {
      auto &pc_comm = *thread.pc_comm;
      auto pb = std::min( k - pc, KC );
      auto is_the_last_pc_iteration = ( pc + KC >= k );

      /*
       *  @CHENHAN: pack image into packB.
       */ 
      auto looppkB = GetRange( 0, jb,      NR, thread.ic_jr, pc_comm.GetNumThreads() ); 
      auto packpkB = GetRange( 0, jb, PACK_NR, thread.ic_jr, pc_comm.GetNumThreads() ); 

      for ( int j   = looppkB.beg(), jp  = packpkB.beg(); 
                j   < looppkB.end(); 
                j  += looppkB.inc(), jp += packpkB.inc() ) 
      {
        auto x0 = ( ( jc + j ) % nx ) * s - p; // top-left
        auto y0 = ( ( jc + j ) / nx ) * s - p; // top-left

#ifdef DEBUG_CONV2D
        printf( "x0 %4d y0 %4d\n", x0, y0 );
#endif

        pack2Dimg<PACK_NR>                            // packB
        (
          std::min( jb - j, NR ), pb, 
          &packB[ jp  * pb ], 
          x0, y0, pc,
          B,
          w0, h0, d0, s, p,
          w1, h1 
        );
      }
      pc_comm.Barrier();


#ifdef DEBUG_CONV2D
      for ( int i = 0; i < pb; i ++ )
      {
        for ( int jj = 0; jj < jb; jj += NR )
        {
          for ( int j = 0; j < NR; j ++ )
          {
            printf( "%5.2lf ", packB[ jj * pb + i * NR + j ] );
          }
          printf( "   " );
        }
        printf( "\n" );
      }
      printf( "\n" );
#endif


      for ( int ic  = loop4th.beg(); 
                ic  < loop4th.end(); 
                ic += loop4th.inc() )                      // beg 4th loop
      {
        auto &ic_comm = *thread.ic_comm;
        auto ib = std::min( m - ic, MC );

        /*
         *  @CHENHAN: assume filters were already packed format.
         */  

        if ( is_the_last_pc_iteration )                    // fused_macro_kernel
        {
          fused_macro_kernel
          <KC, MR, NR, PACK_MR, PACK_NR, MICROKERNEL, TA, TB, TC, TV>
          (
            thread, 
            ic, jc, pc,
            ib, jb, pb,
            packA, 
            packB, 
            C + jc * m + ic, m, 
            microkernel
          );
        }
        else                                               // semiring rank-k update
        {
          rank_k_macro_kernel
          <KC, MR, NR, PACK_MR, PACK_NR, SEMIRINGKERNEL, TA, TB, TC, TV>
          (  
            thread, 
            ic, jc, pc,
            ib, jb, pb,
            packA,
            packB,
            C + jc * m + ic, m, 
            semiringkernel
          );
        }
        ic_comm.Barrier();                                 // sync all jr_id!!
      }                                                    // end 4th loop
      pc_comm.Barrier();
    }                                                      // end 5th loop
  }                                                        // end 6th loop
}                                                          // end cnn_internal





/**
 *  @CHENHAN:
 *
 *  These templates (the same as gkmx.hpp) define a general matrix-matrix multiplication. 
 *  You will be using these existing code to write a convolution operation.
 *  
 *  (First) you should define what parameters you need. For convolution, your
 *  input A will be a image (tensor). B is filters (tensor). C is the output,
 *  which again should be a tensor. Since tensors need more attributes to
 *  describe. You will need to think about what you need instead of m, n, k,
 *  lda, ldb, ldc.
 *
 *  (Second) you need to restructure the loop to loop over each convolution
 *  window. The window size (width*length) is the k dimension of your GEMM.
 *  Notice for each loop in the original GEMM operation you may need more than
 *  one loop in the convolution expression.
 *
 *  The jc loop (6th) will loop over each NC filters.
 *  The pc loop (5th) will loop over each KC elements in one window.
 *  The ic loop (4th) will loop over each MC windows of your image.
 *
 *  You probably don't need to change anything about the macro kernels we
 *  define here (3rd, 2nd loops), since in 4th loop you already transformed the 
 *  problem into a GEMM operation.
 *
 *  (Third) finally you need to write two packing routines and one unpacking
 *  routine. Think about how to pack your image into packA and how to pack your
 *  filters into packB. Finally, you need to reshape your C back to the
 *  original tensor shape.
 *
 *  (Fourth) write a reference function cnn_ref and a test function 
 *  /hmlp/test/test_cnn.cpp to compare your results.
 *
 *  Good luck and have fun!
 *
 *
 */ 
template<
  int MC, int NC, int KC, int MR, int NR, 
  int PACK_MC, int PACK_NC, int PACK_MR, int PACK_NR, int ALIGN_SIZE,
  bool USE_STRASSEN,
  typename SEMIRINGKERNEL, typename MICROKERNEL,
  typename TA, typename TB, typename TC, typename TV>
void conv2d
(
  int w0, int h0, int d0, int s, int p,
  TA *B,
  int w1, int h1, int d1,
  TB *A,
  TC *C,
  SEMIRINGKERNEL semiringkernel, 
  MICROKERNEL microkernel         
)
{
  int jc_nt = 1, pc_nt = 1, ic_nt = 1, jr_nt = 1;
  int nc = NC, pack_nc = PACK_NC;
  char *str;

  int m = d1;
  int nx = ( w0 - w1 + 2 * p ) / s + 1;
  int ny = ( h0 - h1 + 2 * p ) / s + 1;
  int n = nx * ny;
  int k = w1 * h1 * d0;

  //printf( "m %4d n %4d k %4d\n", m, n, k );

  TA *packA_buff = NULL;
  TB *packB_buff = NULL;

  // Early return if possible

  // Check the environment variable.
  jc_nt = hmlp_read_nway_from_env( "KS_JC_NT" );
  ic_nt = hmlp_read_nway_from_env( "KS_IC_NT" );
  jr_nt = hmlp_read_nway_from_env( "KS_JR_NT" );


  if ( jc_nt > 1 )
  {
    nc = ( ( n - 1 ) / ( NR * jc_nt ) + 1 ) * NR;
    pack_nc = ( nc / NR ) * PACK_NR;
  }

  // allocate packing memory
  packA_buff  = hmlp_malloc<ALIGN_SIZE, TA>( KC, ( PACK_MC + 1 ) * jc_nt * ic_nt,         sizeof(TA) );
  packB_buff  = hmlp_malloc<ALIGN_SIZE, TB>( KC, ( pack_nc + 1 ) * jc_nt,                 sizeof(TB) ); 

  // allocate tree communicator
  thread_communicator my_comm( jc_nt, pc_nt, ic_nt, jr_nt );


  #pragma omp parallel num_threads( my_comm.GetNumThreads() ) 
  {
    Worker thread( &my_comm );

    if ( USE_STRASSEN )
    {
      printf( "cnn: strassen algorithms haven't been implemented." );
      exit( 1 );
    }

    conv2d_internal
    <MC, NC, KC, MR, NR, 
    PACK_MC, PACK_NC, PACK_MR, PACK_NR, ALIGN_SIZE,
    USE_STRASSEN,
    SEMIRINGKERNEL, MICROKERNEL,
    TA, TB, TC, TB>
    (
      thread,
      w0, h0, d0, s, p,
      B,
      w1, h1, d1,
      A,
      C,
      semiringkernel, microkernel,
      nc, pack_nc,
      packA_buff,
      packB_buff
    );
  }                                                        // end omp  
}                                                          // end cnn


}; // end namespace cnn
}; // end namespace hmlp



/**
 *  @CHENHAN: write a reference function using GEMM. The signiture of xgemm can
 *  be found in hmlp_blas_lapack.h.
 */ 
template<typename T>
void conv2d_ref( /* Use the same interface as cnn(). */ )
{
}

#endif // define GKMX_HPP
#ifndef SPDASKIT_HPP
#define SPDASKIT_HPP

#include <set>
#include <vector>
#include <deque>
#include <assert.h>
#include <typeinfo>
#include <algorithm>
#include <random>
#include <numeric>
#include <stdio.h>
#include <omp.h>

#include <hmlp.h>
#include <hmlp_blas_lapack.h>
#include <hmlp_util.hpp>
#include <hmlp_thread.hpp>
#include <hmlp_runtime.hpp>
#include <tree.hpp>
#include <skel.hpp>
#include <data.hpp>

//#define DEBUG_SPDASKIT 1


namespace hmlp
{
namespace spdaskit
{



/**
 *  @brief These are data that shared by the whole tree.
 *
 *  @TODO  Make w into a pointer.
 *
 */ 
template<typename SPDMATRIX, typename SPLITTER, typename T>
class Setup : public hmlp::tree::Setup<SPLITTER, T>
{
  public:

    // Number of neighbors
    size_t k;

    // Maximum rank 
    size_t s;

    // Relative error for rank-revealed QR
    T stol;

    // The SPDMATRIX
    SPDMATRIX *K;

    // Weights
    hmlp::Data<T> w;

}; // end class Setup


/**
 *  @brief This class contains all iaskit related data.
 */ 
template<typename T>
class Data
{
  public:

    Lock lock;

    bool isskel = false;

    std::vector<size_t> skels;

    hmlp::Data<T> proj;

    // Weights
    hmlp::Data<T> w_skel;

    hmlp::Data<T> u_skel;

    // Potential
    T u;

    // Events (from HMLP Runtime)
    hmlp::Event skeletonize;

    hmlp::Event updateweight;

}; // end class Data


/**
 *  @brief This class does not need to inherit hmlp::Data<T>, but it should
 *         support two interfaces for data fetching.
 */ 
template<typename T>
class SPDMatrix : public hmlp::Data<T>
{
  public:

    //template<typename TINDEX>
    //std::vector<T> operator()( std::vector<TINDEX> &imap, std::vector<TINDEX> &jmap )
    //{
    //  std::vector<T> submatrix( imap.size() * jmap.size() );
    //  for ( TINDEX j = 0; j < jmap.size(); j ++ )
    //  {
    //    for ( TINDEX i = 0; i < imap.size(); i ++ )
    //    {
    //      submatrix[ j * imap.size() + i ] = (*this)[ d * jmap[ j ] + imap[ i ] ];
    //    }
    //  }
    //  return submatrix;
    //}; 

}; // end class SPDMatrix


/**
 *
 */ 
template<typename NODE>
class Summary
{

  public:

    Summary() {};

    std::deque<hmlp::Statistic> rank;

    std::deque<hmlp::Statistic> skeletonize;

    std::deque<hmlp::Statistic> updateweight;

    void operator() ( NODE *node )
    {
      if ( rank.size() <= node->l )
      {
        rank.push_back( hmlp::Statistic() );
        skeletonize.push_back( hmlp::Statistic() );
        updateweight.push_back( hmlp::Statistic() );
      }

      rank[ node->l ].Update( (double)node->data.skels.size() );
      skeletonize[ node->l ].Update( node->data.skeletonize.GetDuration() );
      updateweight[ node->l ].Update( node->data.updateweight.GetDuration() );

      if ( node->parent )
      {
        auto *parent = node->parent;
        printf( "@TREE\n" );
        printf( "#%lu (s%lu), #%lu (s%lu), %lu, %lu\n", 
            node->treelist_id, node->data.skels.size(), 
            parent->treelist_id, parent->data.skels.size(),
            node->data.skels.size(), node->l );
      }
      else
      {
        printf( "@TREE\n" );
        printf( "#%lu (s%lu), , %lu, %lu\n", 
            node->treelist_id, node->data.skels.size(), 
            node->data.skels.size(), node->l );
      }
    };

    void Print()
    {
      for ( size_t l = 0; l < rank.size(); l ++ )
      {
        printf( "@SUMMARY\n" );
        //rank[ l ].Print();
        skeletonize[ l ].Print();
      }
    };

}; // end class Summary


/**
 *  @brief This the main splitter used to build the Spd-Askit tree.
 *         First compute the approximate center using subsamples.
 *         Then find the two most far away points to do the 
 *         projection.
 *
 *  @TODO  This splitter often fails to produce an even split when
 *         the matrix is sparse.
 *
 */ 
template<int N_SPLIT, typename T>
struct centersplit
{
  // closure
  SPDMatrix<T> *Kptr;

  inline std::vector<std::vector<std::size_t> > operator()
  ( 
    std::vector<std::size_t>& gids,
    std::vector<std::size_t>& lids
  ) const 
  {
    assert( N_SPLIT == 2 );

    SPDMatrix<T> &K = *Kptr;
    size_t N = K.dim();
    size_t n = lids.size();
    std::vector<std::vector<std::size_t> > split( N_SPLIT );

    std::vector<T> temp( n, 0.0 );


    // Compute d2c (distance to center)
    #pragma omp parallel for
    for ( size_t i = 0; i < n; i ++ )
    {
      temp[ i ] = K( lids[ i ], lids[ i ] );
      for ( size_t j = 0; j < std::log( n ); j ++ )
      {
        size_t sample = rand() % n;
        temp[ i ] -= 2.0 * K( lids[ i ], lids[ sample ] );
      }
    }

    // Find the f2c (far most to center)
    auto itf2c = std::max_element( temp.begin(), temp.end() );
    size_t idf2c = std::distance( temp.begin(), itf2c );

    // Compute the d2f (distance to far most)
    #pragma omp parallel for
    for ( size_t i = 0; i < n; i ++ )
    {
      temp[ i ] = K( lids[ i ], lids[ i ] ) - 2.0 * K( lids[ i ], lids[ idf2c ] );
    }

    // Find the f2f (far most to far most)
    auto itf2f = std::max_element( temp.begin(), temp.end() );
    size_t idf2f = std::distance( temp.begin(), itf2f );

#ifdef DEBUG_SPDASKIT
    printf( "idf2c %lu idf2f %lu\n", idf2c, idf2f );
#endif

    // Compute projection
    #pragma omp parallel for
    for ( size_t i = 0; i < n; i ++ )
    {
      temp[ i ] = K( lids[ i ], lids[ idf2f ] ) - K( lids[ i ], lids[ idf2c ] );
    }

    // Parallel median search
    T median = hmlp::tree::Select( n, n / 2, temp );

    split[ 0 ].reserve( n / 2 + 1 );
    split[ 1 ].reserve( n / 2 + 1 );

    // TODO: Can be parallelized
    for ( size_t i = 0; i < n; i ++ )
    {
      if ( temp[ i ] > median ) split[ 1 ].push_back( i );
      else                      split[ 0 ].push_back( i );
    }

    return split;
  };
}; // end struct centersplit


/**
 *  @brief This the splitter used in the randomized tree.
 *
 *  @TODO  This splitter often fails to produce an even split when
 *         the matrix is sparse.
 *
 */ 
template<int N_SPLIT, typename T>
struct randomsplit
{
  // closure
  SPDMatrix<T> *Kptr;

  inline std::vector<std::vector<std::size_t> > operator()
  ( 
    std::vector<std::size_t>& gids,
    std::vector<std::size_t>& lids
  ) const 
  {
    assert( N_SPLIT == 2 );

    SPDMatrix<T> &K = *Kptr;
    size_t N = K.dim();
    size_t n = lids.size();
    std::vector<std::vector<std::size_t> > split( N_SPLIT );
    std::vector<T> temp( n, 0.0 );

    size_t idf2c = std::rand() % n;
    size_t idf2f = std::rand() % n;

    while ( idf2c == idf2f ) idf2f = std::rand() % n;

#ifdef DEBUG_SPDASKIT
    printf( "randomsplit idf2c %lu idf2f %lu\n", idf2c, idf2f );
#endif

    // Compute projection
    #pragma omp parallel for
    for ( size_t i = 0; i < n; i ++ )
    {
      temp[ i ] = K( lids[ i ], lids[ idf2f ] ) - K( lids[ i ], lids[ idf2c ] );
    }

    // Parallel median search
    T median = hmlp::tree::Select( n, n / 2, temp );

    split[ 0 ].reserve( n / 2 + 1 );
    split[ 1 ].reserve( n / 2 + 1 );

    // TODO: Can be parallelized
    for ( size_t i = 0; i < n; i ++ )
    {
      if ( temp[ i ] > median ) split[ 1 ].push_back( i );
      else                      split[ 0 ].push_back( i );
    }

    return split;
  };
}; // end struct randomsplit


/**
 *  @brief This is the task wrapper of the exact KNN search we
 *         perform in the leaf node of the randomized tree.
 *         Currently our heap select cannot deal with duplicate
 *         id; thus, I need to use a std::set to check for the
 *         duplication before inserting the candidate into the
 *         heap.
 *
 *  @TODO  Improve the heap to deal with unique id.
 *
 */ 
template<class NODE, typename T>
class KNNTask : public hmlp::Task
{
  public:

    NODE *arg;

    void Set( NODE *user_arg )
    {
      name = std::string( "neighbor search" );
      arg = user_arg;
      // Need an accurate cost model.
      cost = 1.0;

      //--------------------------------------
      double flops, mops;
      auto &lids = arg->lids;
      auto &NN = *arg->setup->NN;
      flops = lids.size();
      flops *= 4.0 * lids.size();
      // Heap select worst case
      mops = std::log( NN.dim() ) * lids.size();
      mops *= lids.size();
      // Access K
      mops += flops;
      event.Set( flops, mops );
      //--------------------------------------
    };

    void Execute( Worker* user_worker )
    {
      auto &K = *arg->setup->K;
      auto &NN = *arg->setup->NN;
      auto &lids = arg->lids;
     
      #pragma omp parallel for
      for ( size_t j = 0; j < lids.size(); j ++ )
      {
        std::set<size_t> NNset;

        for ( size_t i = 0; i < NN.dim(); i ++ )
        {
          size_t jlid = lids[ j ];
          NNset.insert( NN[ jlid * NN.dim() + i ].second );
        }

        for ( size_t i = 0; i < lids.size(); i ++ )
        {
          size_t ilid = lids[ i ];
          size_t jlid = lids[ j ];

          if ( ilid >= NN.num() ) 
          {
            printf( "ilid %lu exceeds N\n", ilid );
            exit( 1 );
          }


          if ( !NNset.count( ilid ) )
          {
            T dist = K( ilid, ilid ) + K( jlid, jlid ) - 2.0 * K( ilid, jlid );
            std::pair<T, size_t> query( dist, ilid );
            hmlp::HeapSelect( 1, NN.dim(), &query, NN.data() + jlid * NN.dim() );
          }
          else
          {
            // Duplication
          }
        }
      } // end omp parallel for
    };
}; // end class KNNTask

/**
 *
 */ 
template<bool ADAPTIVE, typename NODE>
void Skeletonize( NODE *node )
{

  // Gather shared data and create reference
  auto &K = *node->setup->K;
  auto maxs = node->setup->s;
  auto stol = node->setup->stol;

  // Gather per node data and create reference
  auto &data = node->data;
  auto &skels = data.skels;
  auto &proj = data.proj;
  auto *lchild = node->lchild;
  auto *rchild = node->rchild;

  // Early return if we do not need to skeletonize
  if ( !node->parent ) return;

  if ( ADAPTIVE )
  {
    if ( !node->isleaf && ( !lchild->data.isskel || !rchild->data.isskel ) )
    {
      skels.clear();
      proj.resize( 0, 0 );
      data.isskel = false;
      return;
    }
  }
  else
  {
    //skels.resize( maxs );
    //proj.resize( )
  }

  // random sampling or importance sampling for rows.
  std::vector<size_t> amap;
  std::vector<size_t> bmap;
  std::vector<size_t> &lids = node->lids;


  if ( node->isleaf )
  {
    bmap = node->lids;
  }
  else
  {
    auto &lskels = lchild->data.skels;
    auto &rskels = rchild->data.skels;
    bmap = lskels;
    bmap.insert( bmap.end(), rskels.begin(), rskels.end() );
  }

  auto nsamples = 2 * bmap.size();


  // TODO: random sampling or important sampling for rows. (Severin)
  // Create nsamples.
  // amap = .....
    
   
  // My uniform sample.
  if ( nsamples < K.num() - node->n )
  {
    amap.reserve( nsamples );
    while ( amap.size() < nsamples )
    {
      size_t sample = rand() % K.num();
      if ( std::find( amap.begin(), amap.end(), sample ) == amap.end() &&
           std::find( lids.begin(), lids.end(), sample ) == lids.end() )
      {
        amap.push_back( sample );
      }
    }
  }
  else // Use all off-diagonal blocks without samples.
  {
    for ( int sample = 0; sample < K.num(); sample ++ )
    {
      if ( std::find( amap.begin(), amap.end(), sample ) == amap.end() )
      {
        amap.push_back( sample );
      }
    }
  }

  auto Kab = K( amap, bmap );
  if ( ADAPTIVE )
  {
    hmlp::skel::id( amap.size(), bmap.size(), maxs, stol, Kab, skels, proj );
    data.isskel = skels.size();
  }
  else
  {
    hmlp::skel::id( amap.size(), bmap.size(), maxs, Kab, skels, proj );
    data.isskel = true;
  }

}; // end void Skeletonize()


/**
 *
 */ 
template<bool ADAPTIVE, typename NODE>
class SkeletonizeTask : public hmlp::Task
{
  public:

    NODE *arg;

    void Set( NODE *user_arg )
    {
      name = std::string( "Skeletonization" );
      arg = user_arg;
      // Need an accurate cost model.
      cost = 1.0;
    };

    void GetEventRecord()
    {
      arg->data.skeletonize = event;
    };

    void Execute( Worker* user_worker )
    {
      Skeletonize<ADAPTIVE>( arg );
    };
}; // end class SkeletonizeTask



template<typename NODE>
void UpdateWeights( NODE *node )
{
  // This function computes the skeleton weights.
  if ( !node->parent || !node->data.isskel ) return;

  // Gather shared data and create reference
  auto &w = node->setup->w;

  // Gather per node data and create reference
  auto &data = node->data;
  auto &proj = data.proj;
  auto &skels = data.skels;
  auto &w_skel = data.w_skel;
  auto *lchild = node->lchild;
  auto *rchild = node->rchild;

  // w_skel is s-by-nrhs
  w_skel.clear();
  w_skel.resize( skels.size(), w.dim() );

  if ( node->isleaf )
  {
    auto w_leaf = w( node->lids );

#ifdef DEBUG_SPDASKIT
    printf( "m %lu n %lu k %lu\n", 
      w_skel.dim(), w_skel.num(), w_leaf.num());
    printf( "proj.dim() %lu w_leaf.dim() %lu w_skel.dim() %lu\n", 
        proj.dim(), w_leaf.dim(), w_skel.dim() );
#endif

    xgemm
    (
      "N", "T",
      w_skel.dim(), w_skel.num(), w_leaf.num(),
      1.0, proj.data(),   proj.dim(),
           w_leaf.data(), w_leaf.dim(),
      0.0, w_skel.data(), w_skel.dim()
    );
  }
  else
  {
    auto &w_lskel = lchild->data.w_skel;
    auto &w_rskel = rchild->data.w_skel;
    auto &lskel = lchild->data.skels;
    auto &rskel = rchild->data.skels;
    xgemm
    (
      "N", "N",
      w_skel.dim(), w_skel.num(), lskel.size(),
      1.0, proj.data(),    proj.dim(),
           w_lskel.data(), w_lskel.dim(),
      0.0, w_skel.data(),  w_skel.dim()
    );
    xgemm
    (
      "N", "N",
      w_skel.dim(), w_skel.num(), rskel.size(),
      1.0, proj.data() + proj.dim() * lskel.size(), proj.dim(),
           w_rskel.data(), w_rskel.dim(),
      1.0, w_skel.data(),  w_skel.dim()
    );
  }

}; // end void SetWeights()


/**
 *
 */ 
template<typename NODE>
class UpdateWeightsTask : public hmlp::Task
{
  public:

    NODE *arg;

    void Set( NODE *user_arg )
    {
      name = std::string( "SetWeights" );
      arg = user_arg;
      // Need an accurate cost model.
      cost = 1.0;
    };

    void GetEventRecord()
    {
      arg->data.updateweight = event;
    };

    void Execute( Worker* user_worker )
    {
      UpdateWeights( arg );
    };
}; // end class SetWeights



/**
 *  @brief Compute the interation from column skeletons to row
 *         skeletons. Store the results in the node. Later
 *         there is a SkeletonstoAll function to be called.
 *
 */ 
template<typename NODE>
void SkeletonsToSkeletons( NODE *node )
{
  auto &K = *node->setup.K;
  auto &FarNodes = node->NNFarNodes;
  auto &amap = node->skels;
  auto &u_skel = node->data.u_skel;

  // Initilize u_skel to be zeros( s, nrhs ).
  u_skel.clear();
  u_skel.resize( amap.size(), node->data.w_skel.num(), 0.0 );

  // Reduce all u_skel.
  for ( auto it = FarNodes.begin(); it != FarNodes.end(); it ++ )
  {
    auto &bmap = (*it)->skels;
    auto &w_skel = (*it)->data.w_skel;
    auto Kab = K( amap, bmap );
    assert( w_skel.num() == u_skel.num() );
    xgemm
    (
      "N", "N",
      u_skel.dim(), u_skel.num(), Kab.dim(),
      1.0, Kab.data(),    Kab.dim(),
           w_skel.data(), w_skel.dim(),
      1.0, u_skel.data(), u_skel.dim()
    );
  }

}; // end void SkeletonsToSkeletons()





/**
 *  @brief There is no dependency between each task. However 
 *         there are raw (read after write) dependencies:
 *
 *         NodesToSkeletons (P*w)
 *         SkeletonsToSkeletons ( Sum( Kab * ))
 *
 *
 */ 
template<typename NODE>
class SkeletonsToSkeletonsTask : public hmlp::Task
{
  public:

    NODE *arg;


    void Set( NODE *user_arg )
    {
      name = std::string( "SkeletonsToSkeletons" );
      arg = user_arg;
      // Need an accurate cost model.
      cost = 1.0;
    };

    void GetEventRecord()
    {
      //arg->data.updateweight = event;
    };

    void Execute( Worker* user_worker )
    {
      SkeletonsToSkeletons( arg );
    };
}; // end class SkeletonsToSkeletonsTask


/**
 *  @brief This is a task in Downward traversal. There is data
 *         dependency on u_skel.
 *         
 */ 
template<typename NODE>
void SkeletonsToNodes( NODE *node )
{
}; // end SkeletonsToNodes()











template<typename NODE, typename T>
void Evaluate( NODE *node, NODE *target, hmlp::Data<T> &potentials )
{
  auto &w = node->setup->w;
  auto &lids = node->lids;
  auto &K = *node->setup->K;
  auto &data = node->data;
  auto *lchild = node->lchild;
  auto *rchild = node->rchild;

  auto &amap = target->lids;

  if ( potentials.size() != amap.size() * w.dim() )
  {
    potentials.resize( amap.size(), w.dim(), 0.0 );
  }

  assert( target->isleaf );

  if ( node->isleaf ) // direct evaluation
  {
#ifdef DEBUG_SPDASKIT
    printf( "level %lu direct evaluation\n", node->l );
#endif
    auto Kab = K( amap, lids ); // amap.size()-by-lids.size()
    auto wb  = w( lids ); // nrhs-by-lids.size()
    xgemm
    (
      "N", "T",
      Kab.dim(), wb.dim(), wb.num(),
      1.0, Kab.data(),        Kab.dim(),
           wb.data(),         wb.dim(),
      1.0, potentials.data(), potentials.dim()
    );
  }
  else
  {
    if ( !data.isskel || IsMyParent( target->morton, node->morton ) )
    {
#ifdef DEBUG_SPDASKIT
      printf( "level %lu is not prunable\n", node->l );
#endif
      Evaluate( lchild, target, potentials );      
      Evaluate( rchild, target, potentials );
    }
    else
    {
#ifdef DEBUG_SPDASKIT
      printf( "level %lu is prunable\n", node->l );
#endif
      auto Kab = K( amap, node->data.skels );
      auto &w_skel = node->data.w_skel;
      xgemm
      (
        "N", "N",
        Kab.dim(), w_skel.num(), w_skel.dim(),
        1.0, Kab.data(),        Kab.dim(),
             w_skel.data(),     w_skel.dim(),
        1.0, potentials.data(), potentials.dim()
      );          
    }
  }

}; // end void Evaluate()


/**
 *  @brief This evaluation evaluate the potentils of the whole target node.
 *
 */ 
template<bool SYMMETRIC, bool NNPRUNE, typename NODE>
void Evaluate( NODE *node, NODE *target )
{
  assert( target->isleaf );

  auto &data = node->data;
  auto *lchild = node->lchild;
  auto *rchild = node->rchild;

  std::set<NODE*> *NearNodes;

  if ( NNPRUNE ) NearNodes = &target->NNNearNodes;
  else           NearNodes = &target->NearNodes;

  if ( !data.isskel || node->ContainAny( *NearNodes ) )
  {
    if ( node->isleaf )
    {
      // Do notthing, because the NearNodes list was constructed.
    }
    else
    {
      Evaluate<SYMMETRIC, NNPRUNE>( lchild, target );
      Evaluate<SYMMETRIC, NNPRUNE>( rchild, target );
    }
  }
  else
  {
    if ( SYMMETRIC && node->morton < target->morton )
    {
      // Since target->morton is larger than the visiting node,
      // the interaction between the target and this node has
      // been computed. 
    }
    else
    {
      if ( NNPRUNE ) 
      {
        target->NNFarNodes.insert( node );
      }
      else           
      {
        target->FarNodes.insert( node );
      }
    }
  }
};


template<typename NODE>
void PrintSet( std::set<NODE*> &set )
{
  for ( auto it = set.begin(); it != set.end(); it ++ )
  {
    printf( "%lu, ", (*it)->treelist_id );
  }
  printf( "\n" );
};


/**
 *  @brief Compute those near leaf nodes and build a list. This is just like
 *         the neighbor list but the granularity is in nodes but not points.
 *         The algorithm is to compute the node morton ids of neighbor points.
 *         Get the pointers of these nodes and insert them into a std::set.
 *         std::set will automatic remove duplication. Here the insertion 
 *         will be performed twice each time to get a symmetric one. That is
 *         if alpha has beta in its list, then beta will also have alpha in
 *         its list.
 *
 *         Only leaf nodes will have the list `` NearNodes''.
 *
 *         This list will later be used to get the FarNodes using a recursive
 *         node traversal scheme.
 *  
 */ 
template<bool SYMMETRIC, bool NNPRUNE, typename TREE>
void NearNodes( TREE &tree )
{
  auto &setup = tree.setup;
  auto &NN = *setup.NN;
  size_t n_nodes = 1 << tree.depth;
  auto level_beg = tree.treelist.begin() + n_nodes - 1;

  //printf( "NN( %lu, %lu ) depth %lu n_nodes %lu treelist.size() %lu\n", 
  //    NN.dim(), NN.num(),      
  //    tree.depth, n_nodes, tree.treelist.size() );


  // Traverse all leaf nodes. 
  //#pragma omp parallel for
  for ( size_t node_ind = 0; node_ind < n_nodes; node_ind ++ )
  {
    auto *node = *(level_beg + node_ind);
    auto &data = node->data;

    if ( NNPRUNE )
    {
      // Add myself to the list.
      node->NNNearNodes.insert( node );
      // Traverse all points and their neighbors. NN is stored in k-by-N.
      // NN 
      for ( size_t j = 0; j < node->lids.size(); j ++ )
      {
        size_t lid = node->lids[ j ];
        for ( size_t i = 0; i < NN.dim(); i ++ )
        {
          size_t neighbor_gid = NN( i, lid ).second;
          //printf( "lid %lu i %lu neighbor_gid %lu\n", lid, i, neighbor_gid );
          size_t neighbor_lid = tree.Getlid( neighbor_gid );
          size_t neighbor_morton = setup.morton[ neighbor_lid ];
          //printf( "neighborlid %lu morton %lu\n", neighbor_lid, neighbor_morton );

          node->NNNearNodes.insert( tree.Morton2Node( neighbor_morton ) );
        }
      }
    }
    else
    {
      // Add myself to the list, and it's done.
      node->NearNodes.insert( node );
    }
  }

  if ( SYMMETRIC && NNPRUNE )
  {
    // Make it symmetric
    for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
    {
      auto *node = *(level_beg + node_ind);
      auto &NNNearNodes = node->NNNearNodes;
      for ( auto it = NNNearNodes.begin(); it != NNNearNodes.end(); it ++ )
      {
        (*it)->NNNearNodes.insert( node );
      }
    }
#ifdef DEBUG_SPDASKIT
    for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
    {
      auto *node = *(level_beg + node_ind);
      auto &NNNearNodes = node->NNNearNodes;
      printf( "Node %lu NearNodes ", node->treelist_id );
      for ( auto it = NNNearNodes.begin(); it != NNNearNodes.end(); it ++ )
      {
        printf( "%lu, ", (*it)->treelist_id );
      }
      printf( "\n" );
    }
#endif
  }
};

/**
 *  TODO: change to task.
 *
 */
template<bool SYMMETRIC, bool NNPRUNE, typename TREE>
void FarNodes( TREE &tree )
{

  for ( int l = tree.depth; l >= 0; l -- )
  {
    std::size_t n_nodes = 1 << l;
    auto level_beg = tree.treelist.begin() + n_nodes - 1;

    for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
    {
      auto *node = *(level_beg + node_ind);

      if ( node->isleaf )
      {
        Evaluate<SYMMETRIC, NNPRUNE>( tree.treelist[ 0 ], node );
      }
      else
      {
        // Merging FarNodes from children
        auto *lchild = node->lchild;
        auto *rchild = node->rchild;
        if ( NNPRUNE )
        {
          auto &pFarNodes =   node->NNFarNodes;
          auto &lFarNodes = lchild->NNFarNodes;
          auto &rFarNodes = rchild->NNFarNodes;

          for ( auto it = lFarNodes.begin(); it != lFarNodes.end(); ++ it )
          {
            if ( rFarNodes.count( *it ) ) pFarNodes.insert( *it );
          }
          for ( auto it = pFarNodes.begin(); it != pFarNodes.end(); it ++ )
          {
            lFarNodes.erase( *it );
            rFarNodes.erase( *it );
          }

        }
        else
        {
          auto &pFarNodes =   node->FarNodes;
          auto &lFarNodes = lchild->FarNodes;
          auto &rFarNodes = rchild->FarNodes;
          for ( auto it = lFarNodes.begin(); it != lFarNodes.end(); it ++ )
          {
            if ( rFarNodes.count( *it ) ) pFarNodes.insert( *it );
          }
          for ( auto it = pFarNodes.begin(); it != pFarNodes.end(); it ++ )
          {
            lFarNodes.erase( *it );
            rFarNodes.erase( *it );
          }
        }
      }
    }
  }

  if ( SYMMETRIC )
  {
    // Symmetrinize FarNodes to FarNodes interaction.
    for ( int l = tree.depth; l >= 0; l -- )
    {
      std::size_t n_nodes = 1 << l;
      auto level_beg = tree.treelist.begin() + n_nodes - 1;

      for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
      {
        auto *node = *(level_beg + node_ind);
        auto &pFarNodes =   node->NNFarNodes;
        for ( auto it = pFarNodes.begin(); it != pFarNodes.end(); it ++ )
        {
          (*it)->NNFarNodes.insert( node );
        }
      }
    }
  }
  
#ifdef DEBUG_SPDASKIT
  for ( int l = tree.depth; l >= 0; l -- )
  {
    std::size_t n_nodes = 1 << l;
    auto level_beg = tree.treelist.begin() + n_nodes - 1;

    for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
    {
      auto *node = *(level_beg + node_ind);
      auto &pFarNodes =   node->NNFarNodes;
      for ( auto it = pFarNodes.begin(); it != pFarNodes.end(); it ++ )
      {
        if ( !( (*it)->NNFarNodes.count( node ) ) )
        {
          printf( "Unsymmetric FarNodes %lu, %lu\n", node->treelist_id, (*it)->treelist_id );
          printf( "Near\n" );
          PrintSet(  node->NNNearNodes );
          PrintSet( (*it)->NNNearNodes );
          printf( "Far\n" );
          PrintSet(  node->NNFarNodes );
          PrintSet( (*it)->NNFarNodes );
          printf( "======\n" );
          break;
        }
      }
      if ( pFarNodes.size() )
      {
        printf( "l %2lu FarNodes(%lu) ", node->l, node->treelist_id );
        PrintSet( pFarNodes );
      }
    }
  }
#endif
};


/**
 *  @brief Create a list of near nodes for  each leave (direct evaluation).
 *         Create a list of far nodes for each node (low-rank).
 *
 *         If ( SYMMETRIC ) Both near and far lists are symmetric.
 *         If ( NNPRUNE )   All neighbors are near nodes. Lists stored in
 *                          NNNearNodes and NNFarNodes.
 *         Else             Only the leave itself is the near node. Use
 *                          NearNodes and FarNodes.
 *        
 */ 
template<bool SYMMETRIC, bool NNPRUNE, typename TREE>
void NearFarNodes( TREE &tree )
{
  //printf( "Enter NearNodes\n" );
  NearNodes<SYMMETRIC, NNPRUNE>( tree );
  //printf( "Finish NearNodes\n" );
  FarNodes<SYMMETRIC, NNPRUNE>( tree );
};


template<bool NNPRUNE, typename TREE>
void DrawInteraction( TREE &tree )
{
  FILE * pFile;
  int n;
  char name [100];

  pFile = fopen ( "interaction.m", "w" );


  fprintf( pFile, "figure('Position',[100,100,800,800]);" );
  fprintf( pFile, "hold on;" );
  fprintf( pFile, "axis square;" );
  fprintf( pFile, "axis ij;" );

  for ( int l = tree.depth; l >= 0; l -- )
  {
    std::size_t n_nodes = 1 << l;
    auto level_beg = tree.treelist.begin() + n_nodes - 1;

    for ( int node_ind = 0; node_ind < n_nodes; node_ind ++ )
    {
      auto *node = *(level_beg + node_ind);

      if ( NNPRUNE )
      {
        auto &pNearNodes = node->NNNearNodes;
        auto &pFarNodes = node->NNFarNodes;
        for ( auto it = pFarNodes.begin(); it != pFarNodes.end(); it ++ )
        {
          double gb = (double)std::min( node->l, (*it)->l ) / tree.depth;
          //printf( "node->l %lu (*it)->l %lu depth %lu\n", node->l, (*it)->l, tree.depth );
          fprintf( pFile, "rectangle('position',[%lu %lu %lu %lu],'facecolor',[1.0,%lf,%lf]);\n",
              node->offset,      (*it)->offset,
              node->lids.size(), (*it)->lids.size(),
              gb, gb );
        }
        for ( auto it = pNearNodes.begin(); it != pNearNodes.end(); it ++ )
        {
          fprintf( pFile, "rectangle('position',[%lu %lu %lu %lu],'facecolor',[0.2,0.4,1.0]);\n",
              node->offset,      (*it)->offset,
              node->lids.size(), (*it)->lids.size() );
        }  
      }
      else
      {
      }
    }
  }
  fprintf( pFile, "hold off;" );
  fclose( pFile );
};




/**
 *  @breif This is a fake evaluation setup aimming to figure out
 *         which tree node will prun which points. The result
 *         will be stored in each node as two lists, prune and noprune.
 *
 */ 
template<bool SYMBOLIC, bool NNPRUNE, typename NODE, typename T>
void Evaluate
( 
  NODE *node, 
  size_t lid, 
  std::vector<size_t> &nnandi, // k + 1 non-prunable lists
  hmlp::Data<T> &potentials 
)
{
  auto &w = node->setup->w;
  auto &lids = node->lids;
  auto &K = *node->setup->K;
  auto &data = node->data;
  auto *lchild = node->lchild;
  auto *rchild = node->rchild;

  auto amap = std::vector<size_t>( 1 );

  amap[ 0 ] = lid;

  if ( !SYMBOLIC ) // No potential evaluation.
  {
    assert( potentials.size() == amap.size() * w.dim() );
  }

  if ( !data.isskel || node->ContainAny( nnandi ) )
  {
    //printf( "level %lu is not prunable\n", node->l );
    if ( node->isleaf )
    {
      if ( SYMBOLIC )
      {
        data.lock.Acquire();
        {
          // Add lid to notprune list. We use a lock.
          if ( NNPRUNE ) 
          {
            node->NNNearIDs.insert( lid );
          }
          else           
          {
            node->NearIDs.insert( lid );
          }
        }
        data.lock.Release();
      }
      else
      {
#ifdef DEBUG_SPDASKIT
        printf( "level %lu direct evaluation\n", node->l );
#endif
        auto Kab = K( amap, lids ); // amap.size()-by-lids.size()
        auto wb  = w( lids ); // nrhs-by-lids.size()
        xgemm
        (
          "N", "T",
          Kab.dim(), wb.dim(), wb.num(),
          1.0, Kab.data(),        Kab.dim(),
          wb.data(),         wb.dim(),
          1.0, potentials.data(), potentials.dim()
        );
      }
    }
    else
    {
      Evaluate<SYMBOLIC, NNPRUNE>( lchild, lid, nnandi, potentials );      
      Evaluate<SYMBOLIC, NNPRUNE>( rchild, lid, nnandi, potentials );
    }
  }
  else // need lid's morton and neighbors' mortons
  {
    //printf( "level %lu is prunable\n", node->l );
    if ( SYMBOLIC )
    {
      data.lock.Acquire();
      {
        // Add lid to prunable list.
        if ( NNPRUNE ) 
        {
          node->FarIDs.insert( lid );
        }
        else           
        {
          node->NNFarIDs.insert( lid );
        }
      }
      data.lock.Release();
    }
    else
    {
#ifdef DEBUG_SPDASKIT
      printf( "level %lu is prunable\n", node->l );
#endif
      auto Kab = K( amap, node->data.skels );
      auto &w_skel = node->data.w_skel;
      xgemm
      (
        "N", "N",
        Kab.dim(), w_skel.num(), w_skel.dim(),
        1.0, Kab.data(),        Kab.dim(),
        w_skel.data(),     w_skel.dim(),
        1.0, potentials.data(), potentials.dim()
      );          
    }
  }



}; // end T Evaluate()


template<bool SYMBOLIC, bool NNPRUNE, typename TREE, typename T>
void Evaluate
( 
  TREE &tree, 
  size_t gid, 
  hmlp::Data<T> &potentials
)
{
  std::vector<size_t> nnandi;
  auto &w = tree.setup.w;
  size_t lid = tree.Getlid( gid );

  potentials.clear();
  potentials.resize( 1, w.dim(), 0.0 );

  if ( NNPRUNE )
  {
    auto &NN = *tree.setup.NN;
    nnandi.reserve( NN.dim() + 1 );
    nnandi.push_back( lid );
    for ( size_t i = 0; i < NN.dim(); i ++ )
    {
      nnandi.push_back( tree.Getlid( NN[ lid * NN.dim() + i ].second ) );
    }
#ifdef DEBUG_SPDASKIT
    printf( "nnandi.size() %lu\n", nnandi.size() );
#endif
  }
  else
  {
    nnandi.reserve( 1 );
    nnandi.push_back( lid );
  }

  Evaluate<SYMBOLIC, NNPRUNE>( tree.treelist[ 0 ], lid, nnandi, potentials );

}; // end void Evaluate()



template<typename NODE, typename T>
void ComputeError( NODE *node, hmlp::Data<T> potentials )
{
  auto &K = *node->setup->K;
  auto &w = node->setup->w;
  auto &lids = node->lids;

  auto &amap = node->lids;
  std::vector<size_t> bmap = std::vector<size_t>( K.num() );

  for ( size_t j = 0; j < bmap.size(); j ++ ) bmap[ j ] = j;

  auto Kab = K( amap, bmap );

  auto nrm2 = hmlp_norm( potentials.dim(), potentials.num(), 
                         potentials.data(), potentials.dim() ); 

  xgemm
  (
    "N", "T",
    Kab.dim(), w.dim(), w.num(),
    -1.0, Kab.data(),        Kab.dim(),
          w.data(),          w.dim(),
     1.0, potentials.data(), potentials.dim()
  );          

  auto err = hmlp_norm( potentials.dim(), potentials.num(), 
                        potentials.data(), potentials.dim() ); 
  
  printf( "node relative error %E, nrm2 %E\n", err / nrm2, nrm2 );


}; // end void ComputeError()








/**
 *  @brief
 */ 
template<typename TREE, typename T>
T ComputeError( TREE &tree, size_t gid, hmlp::Data<T> potentials )
{
  auto &K = *tree.setup.K;
  auto &w = tree.setup.w;
  auto lid = tree.Getlid( gid );

  auto amap = std::vector<size_t>( 1 );
  auto bmap = std::vector<size_t>( K.num() );
  amap[ 0 ] = lid;
  for ( size_t j = 0; j < bmap.size(); j ++ ) bmap[ j ] = j;

  auto Kab = K( amap, bmap );

  auto nrm2 = hmlp_norm( potentials.dim(), potentials.num(), 
                         potentials.data(), potentials.dim() ); 

  xgemm
  (
    "N", "T",
    Kab.dim(), w.dim(), w.num(),
    -1.0, Kab.data(),        Kab.dim(),
          w.data(),          w.dim(),
     1.0, potentials.data(), potentials.dim()
  );          

  auto err = hmlp_norm( potentials.dim(), potentials.num(), 
                        potentials.data(), potentials.dim() ); 

  return err / nrm2;
}; // end T ComputeError()



}; // end namespace spdaskit
}; // end namespace hmlp


#endif // define SPDASKIT_HPP
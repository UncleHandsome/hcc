// XFAIL: Linux
// RUN: %hc %s -o %t.out && %t.out

#include <hc.hpp>

#include <iostream>

// loop to deliberately slow down kernel execution
#define LOOP_COUNT (1024)

#define TEST_DEBUG (0)

/// test implicit synchronization of array_view and kernel dispatches
///
template<size_t grid_size, size_t tile_size>
bool test1D() {

  bool ret = true;

  // dependency graph
  // pfe1: av1 + av2 -> av3
  // pfe2: av1 - av2 -> av4
  // pfe3: av3 * av4 -> av5 
  // pfe1 and pfe2 are independent
  // pfe3 depends on pfe1 and pfe2

  std::vector<int> table1(grid_size);
  std::vector<int> table2(grid_size);
  std::vector<int> table3(grid_size);
  std::vector<int> table4(grid_size);
  std::vector<int> table5(grid_size);

  for (int i = 0; i < grid_size; ++i) {
    table1[i] = i;
    table2[i] = i;
  }

  hc::array_view<const int, 1> av1(grid_size, table1);
  hc::array_view<const int, 1> av2(grid_size, table2);
  hc::array_view<int, 1> av3(grid_size, table3);
  hc::array_view<int, 1> av4(grid_size, table3);
  hc::array_view<int, 1> av5(grid_size, table3);

#if TEST_DEBUG
  std::cout << "launch pfe1\n";
#endif

  hc::parallel_for_each(hc::extent<1>(grid_size), [=](hc::index<1>& idx) restrict(amp) {
    // av3 = i * 2
    for (int i = 0; i < LOOP_COUNT; ++i)
      av3(idx) = av1(idx) + av2(idx);
  });

#if TEST_DEBUG
  std::cout << "after pfe1\n";
#endif

#if TEST_DEBUG
  std::cout << "launch pfe2\n";
#endif

  // this kernel dispatch shall NOT implicitly wait for the previous one to complete
  // because av1 and av2 are read-only, and this kernel writes to av4, NOT av3
  hc::parallel_for_each(hc::extent<1>(grid_size), [=](hc::index<1>& idx) restrict(amp) {
    // av4 = 0
    for (int i = 0; i < LOOP_COUNT; ++i)
      av4(idx) = av1(idx) - av2(idx);
  });

#if TEST_DEBUG
  std::cout << "after pfe2\n";
#endif

  // now there must be 2 pending async operations for the accelerator_view
  // because pfe1 and pfe2 are independent
  ret &= (hc::accelerator().get_default_view().get_pending_async_ops() == 2);

#if TEST_DEBUG
  std::cout << "launch pfe3\n";
#endif

  // this kernel dispatch shall implicitly wait for the previous two to complete
  // because they access the same array_view instances and write to them
  hc::parallel_for_each(hc::extent<1>(grid_size), [=](hc::index<1>& idx) restrict(amp) {
    // av5 = 0
    for (int i = 0; i < LOOP_COUNT; ++i)
      av5(idx) = av3(idx) * av4(idx);
  });

#if TEST_DEBUG
  std::cout << "after pfe3\n";
#endif

  // now there must be 1 pending async operations for the accelerator_view
  // pfe1 and pfe2 must be completed by now
  ret &= (hc::accelerator().get_default_view().get_pending_async_ops() == 1);

  // for this test case we deliberately NOT wait on kernels
  // we want to check when array_view instances go to destruction
  // would all dependent kernels be waited or not 

  return ret;
}

int main() {
  bool ret = true;

  hc::accelerator_view av = hc::accelerator().get_default_view();

  ret &= test1D<32, 16>();
  ret &= (av.get_pending_async_ops() == 0);
  ret &= test1D<64, 8>();
  ret &= (av.get_pending_async_ops() == 0);
  ret &= test1D<128, 32>();
  ret &= (av.get_pending_async_ops() == 0);
  ret &= test1D<256, 64>();
  ret &= (av.get_pending_async_ops() == 0);
  ret &= test1D<1024, 256>();
  ret &= (av.get_pending_async_ops() == 0);

  return !(ret == true);
}


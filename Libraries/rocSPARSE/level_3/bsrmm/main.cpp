// MIT License
//
// Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "rocsparse_utils.hpp"

#include <rocsparse/rocsparse.h>

#include <hip/hip_runtime.h>

#include <cstdio>
#include <iostream>

int main()
{
    // 1. Setup input data
    // Matrix A (m x k)
    //     ( 1 2 0 3 0 0 )
    // A = ( 0 4 5 0 0 0 )
    //     ( 0 0 0 7 8 0 )
    //     ( 0 0 1 2 4 1 )

    // Number of rows and columns
    constexpr rocsparse_int block_dim = 2;
    constexpr rocsparse_int mb        = 2;
    constexpr rocsparse_int kb        = 3;
    constexpr rocsparse_int n         = 10;
    constexpr rocsparse_int m         = mb * block_dim;
    constexpr rocsparse_int k         = kb * block_dim;

    // Number of non-zero block entries
    constexpr rocsparse_int nnzb = 4;

    // BSR row pointers
    constexpr rocsparse_int h_bsr_row_ptr[3] = {0, 2, 4};

    // BSR column indices
    constexpr rocsparse_int hbsr_col_ind[4] = {0, 1, 1, 2};

    // BSR values
    constexpr double h_bsr_val[16]
        = {1.0, 2.0, 0.0, 4.0, 0.0, 3.0, 5.0, 0.0, 0.0, 7.0, 1.0, 2.0, 8.0, 0.0, 4.0, 1.0};

    // Transposition of the matrix
    constexpr rocsparse_direction dir     = rocsparse_direction_row;
    constexpr rocsparse_operation trans_A = rocsparse_operation_none;
    constexpr rocsparse_operation trans_B = rocsparse_operation_none;

    // Matrix B (k x n) column major order
    //     ( 9  11 13 15 17 10 12 14 16 18 )
    //     ( 8  10 1  10 6  11 7  3  12 17 )
    // B = ( 11 11 0  4  6  12 2  9  13 2  )
    //     ( 15 3  2  3  8  1  2  4  6  6  )
    //     ( 2  5  7  0  1  15 9  4  10 1  )
    //     ( 7  12 12 1  12 5  1  11 1  14 )

    // Matrix B in column-major
    const rocsparse_int ldb = k;
    constexpr double    h_B[6 * 10]
        = {9, 8, 11, 15, 2,  7, 11, 10, 11, 3,  5,  12, 13, 1, 0,  2,  7,  12, 15, 10,
           4, 3, 0,  1,  17, 6, 6,  8,  1,  12, 10, 11, 12, 1, 15, 5,  12, 7,  2,  2,
           9, 1, 14, 3,  9,  4, 4,  11, 16, 12, 13, 6,  10, 1, 18, 17, 2,  6,  1,  14};

    // Matrix C (m x n) column major order
    //     ( 0 0 0 0 0 0 0 0 0 0 )
    // C = ( 0 0 0 0 0 0 0 0 0 0 )
    //     ( 0 0 0 0 0 0 0 0 0 0 )
    //     ( 0 0 0 0 0 0 0 0 0 0 )

    // Matrix C (m x n) in column-major
    const rocsparse_int ldc         = m;
    double              h_C[4 * 10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Scalar alpha and beta
    constexpr double alpha = 1.0;
    constexpr double beta  = 0.0;

    // 2. Prepare device for calculation
    // rocSPARSE handle
    rocsparse_handle handle;
    ROCSPARSE_CHECK(rocsparse_create_handle(&handle));

    // Matrix descriptor
    rocsparse_mat_descr descr;
    ROCSPARSE_CHECK(rocsparse_create_mat_descr(&descr));

    // 3. Offload data to device
    rocsparse_int* d_bsr_row_ptr;
    rocsparse_int* d_bsr_col_ind;
    double*        d_bsr_val;
    double*        d_B;
    double*        d_C;

    const size_t size_B       = sizeof(*d_C) * k * n;
    const size_t size_C       = sizeof(*d_C) * m * n;
    const size_t size_val     = sizeof(*d_bsr_val) * nnzb * block_dim * block_dim;
    const size_t size_row_ptr = sizeof(*d_bsr_row_ptr) * (mb + 1);
    const size_t size_col_ind = sizeof(*d_bsr_row_ptr) * nnzb;

    HIP_CHECK(hipMalloc((void**)&d_bsr_row_ptr, size_row_ptr));
    HIP_CHECK(hipMalloc((void**)&d_bsr_col_ind, size_col_ind));
    HIP_CHECK(hipMalloc((void**)&d_bsr_val, size_val));
    HIP_CHECK(hipMalloc((void**)&d_B, size_B));
    HIP_CHECK(hipMalloc((void**)&d_C, size_C));

    HIP_CHECK(hipMemcpy(d_bsr_row_ptr, h_bsr_row_ptr, size_row_ptr, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_bsr_col_ind, hbsr_col_ind, size_col_ind, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_bsr_val, h_bsr_val, size_val, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_B, h_B, size_B, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_C, h_C, size_C, hipMemcpyHostToDevice));

    // 4. Call bsrmm to perform C = alpha * A' * B' + beta * C
    ROCSPARSE_CHECK(rocsparse_dbsrmm(handle,
                                     dir,
                                     trans_A,
                                     trans_B,
                                     mb,
                                     n,
                                     kb,
                                     nnzb,
                                     &alpha,
                                     descr,
                                     d_bsr_val,
                                     d_bsr_row_ptr,
                                     d_bsr_col_ind,
                                     block_dim,
                                     d_B,
                                     ldb,
                                     &beta,
                                     d_C,
                                     ldc));

    // 5. Copy y to host from device
    HIP_CHECK(hipMemcpy(h_C, d_C, size_C, hipMemcpyDeviceToHost));

    // 6. Clear rocSPARSE
    ROCSPARSE_CHECK(rocsparse_destroy_handle(handle));
    ROCSPARSE_CHECK(rocsparse_destroy_mat_descr(descr));

    // 7. Clear device memory
    HIP_CHECK(hipFree(d_bsr_row_ptr));
    HIP_CHECK(hipFree(d_bsr_col_ind));
    HIP_CHECK(hipFree(d_bsr_val));
    HIP_CHECK(hipFree(d_B));
    HIP_CHECK(hipFree(d_C));

    // 8. Print result
    std::cout << "C =" << std::endl;

    for(int i = 0; i < m; ++i)
    {
        std::cout << "    (";
        for(int j = 0; j < n; ++j)
        {
            std::printf("%5.0lf", h_C[i + j * ldc]);
        }

        std::cout << " )" << std::endl;
    }

    return 0;
}

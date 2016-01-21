#include <coarse/conv_coarse_svm.h>

#include <CL/cl.h>

int CoarseSVM_ApplyStencil(real32* in_img, uint32 img_width,
                           uint32 img_height, real32* msk,
                           uint32 msk_width, uint32 msk_height,
                           real32* out_img, bool use_unrolled)
{
    std::cout << "Coarse (SVM) Convolution START!" << std::endl;

    if(use_unrolled)
    {
        std::cout << "Urolled kernel used";
    }
    else
    {
        std::cout << "Urolled kernel used NOT used";        
    }
    std::cout << std::endl;

    TestCaseStarted();
    
    _img_width = img_width;
    _img_height = img_height;
    _msk_width = msk_width;
    _msk_height = msk_height;
    _half_w = msk_width / 2;
    _half_h = msk_height / 2;

    _img_size = img_width * img_height;
    _inner_width = (img_width - (_half_w*2));
    _inner_height = (img_height - (_half_h*2));
    _inner_size = _inner_width * _inner_height;
    _msk_size = msk_width * msk_height;
        
    cl_int status;

    status = AllocateSVMObjects();
    CHECK_ERROR(status, "AllocateSVMObjects");
    status = SVMHandleAllBoundries(in_img, msk, out_img);
    CHECK_ERROR(status, "SVMHandleAllBoundries");

    cl_event kernel_evt;
    status = SVMHandleInnerRegions(use_unrolled, &kernel_evt);
    CHECK_ERROR(status, "SVMHandleInnerRegions");
    clWaitForEvents(1, &kernel_evt);
    status = CopyOutputFromSVM(out_img);
    CHECK_ERROR(status, "CopyOutputFromSVM");

    TestCaseFinished();
    
    clSVMFree(context, _in_img);
    clSVMFree(context, _msk);
    clSVMFree(context, _out_img);
        
    // Print2DArray("Output Image: ", out_img, img_width,
    //              img_height);
    
    std::cout << "Coarse (SVM) Convolution FINISH!" << std::endl;
    std::cout << "======================" << std::endl;
    return SUCCESS;
}

int AllocateSVMObjects()
{
    _in_img = (real32*)clSVMAlloc(context, CL_MEM_READ_WRITE,
                                  _img_size*sizeof(real32), 0);
    CHECK_ALLOCATION(_in_img, "_in_img");
        
    _msk = (real32*)clSVMAlloc(context, CL_MEM_READ_WRITE,
                               _msk_size*sizeof(real32), 0);
    CHECK_ALLOCATION(_msk, "_msk");

    _out_img = (real32*)clSVMAlloc(
        context, CL_MEM_READ_WRITE, _img_size*sizeof(real32), 0);
    CHECK_ALLOCATION(_out_img, "_out_img");
    
    return SUCCESS;
}

int SVMHandleAllBoundries(real32* in_img, real32* msk,
                             real32* out_img)
{
    cl_int status;
    
    // Map SVM objects into the host address space to copy input
    // data and handle the boundaries
    status = clEnqueueSVMMap(queue, CL_TRUE,
                             CL_MAP_WRITE_INVALIDATE_REGION,
                             _in_img, _img_size*sizeof(real32),
                             0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMMap");
    // TODO: this will be replaced by initialization after making each
    // implementation responsilbe for its memory allocations
    std::memcpy(_in_img, in_img, _img_size*sizeof(real32));

    status = clEnqueueSVMMap(queue, CL_TRUE,
                             CL_MAP_WRITE_INVALIDATE_REGION,
                             _msk, _msk_size*sizeof(real32),
                             0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMMap");
    std::memcpy(_msk, msk, _msk_size*sizeof(real32));

    status = clEnqueueSVMMap(queue, CL_TRUE,
                             CL_MAP_WRITE_INVALIDATE_REGION,
                             _out_img, _img_size*sizeof(real32),
                             0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMMap");

    HandleAllBoundries();
        
    status = clEnqueueSVMUnmap(queue, _in_img, 0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMUnmap");
    status = clEnqueueSVMUnmap(queue, _msk, 0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMUnmap");
    status = clEnqueueSVMUnmap(queue, _out_img, 0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMUnmap");
    
    return status;
}

int SVMHandleInnerRegions(bool use_unrolled, cl_event* kernel_evt)
{
    int status;
    cl_kernel kernel;

        
    if(use_unrolled)
    {
        status = SetupKernel("conv_kernel_unrolled.cl",
                             "conv_kernel", &kernel);
    }
    else
    {
        status = SetupKernel("conv_kernel.cl", "conv_kernel", &kernel);
    }
    CHECK_ERROR(status, "SetupKernel");

    int arg_idx = 0;
    status = clSetKernelArgSVMPointer(kernel, arg_idx++, _in_img);
    CHECK_OPENCL_ERROR(status, "clSetKernelArgSVMPointer");

    status = clSetKernelArg(kernel, arg_idx++, sizeof(uint32), &_img_width);
    CHECK_OPENCL_ERROR(status, "clSetKernelArg");

    status = clSetKernelArg(kernel, arg_idx++, sizeof(uint32), &_img_height);
    CHECK_OPENCL_ERROR(status, "clSetKernelArg");
    
    status = clSetKernelArgSVMPointer(kernel,  arg_idx++, _msk);
    CHECK_OPENCL_ERROR(status, "clSetKernelArgSVMPointer");

    if(!use_unrolled)
    {
        status = clSetKernelArg(kernel, arg_idx++, sizeof(uint32), &_msk_width);
        CHECK_OPENCL_ERROR(status, "clSetKernelArg");
    }   

    status = clSetKernelArgSVMPointer(kernel, arg_idx++, _out_img);
    CHECK_OPENCL_ERROR(status, "clSetKernelArgSVMPointer");

    // NOTE: despite having a max group size of 256 (16x16) on this
    // machine, the kernel fails to produce the correct results when
    // using svm. the max power of 2 that it accepts is 1!! the reason
    // is that the work group must be a multiple of the local
    // size. this can be overcome in coarse SVM because we will have
    // to map and unmpa any way but in fine grained access
    
    // as Arsene suggested I justed added an if condition to the
    // kernel code for now. only a few number of groups will diverge
    // making the overhead neglegible
    size_t local_dim = 16;
    
    size_t global[2];
    global[0] = (((_inner_width - 1) / local_dim) + 1) * local_dim;
    global[1] = (((_inner_height - 1) / local_dim) + 1) * local_dim;

    size_t local[2];
    local[0] = local_dim;
    local[1] = local_dim;

    status = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global,
                                    local, 0, NULL, kernel_evt);
    CHECK_OPENCL_ERROR(status, "clEnqueueNDRangeKernel");

    return SUCCESS;
}

cl_int CopyOutputFromSVM(real32* out_img)
{
    cl_int status;
    status = clEnqueueSVMMap(queue, CL_TRUE, CL_MAP_READ,
                             _out_img, _img_size*sizeof(real32),
                             0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMMap");
    
    std::memcpy(out_img, _out_img, _img_size*sizeof(real32));

    status = clEnqueueSVMUnmap(queue, _out_img, 0, NULL, NULL);
    CHECK_OPENCL_ERROR(status, "clEnqueueSVMUnmap");
    
    return status;
}

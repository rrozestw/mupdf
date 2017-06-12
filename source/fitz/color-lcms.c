#include "mupdf/fitz.h"
#include "lcms2.h"
#include "lcms2_plugin.h"
#include "colorspace-imp.h"

#define LCMS_BYTES_MASK 0x7
/* #define DEBUG_LCMS_MEM(A) do { printf A; fflush(stdout); } while (0) */
#define DEBUG_LCMS_MEM(A) do { } while (0)

static void
fz_lcms_error(cmsContext id, cmsUInt32Number error_code, const char *error_text)
{
	fz_context *ctx = (fz_context *)cmsGetContextUserData(id);
	fz_warn(ctx, "lcms error: %s", error_text);
}

static void
*fz_lcms_malloc(cmsContext id, unsigned int size)
{
	void *result;
	fz_context *ctx = (fz_context *)cmsGetContextUserData(id);
	result = fz_malloc_no_throw(ctx, size);
	DEBUG_LCMS_MEM(("Allocation::  mupdf ctx = %p lcms ctx = %p allocation = %p \n", (void*) ctx, (void*) id, (void*) result));
	return result;
}

static void
fz_lcms_free(cmsContext id, void *ptr)
{
	fz_context *ctx = (fz_context *)cmsGetContextUserData(id);
	DEBUG_LCMS_MEM(("Free:: mupdf ctx = %p lcms ctx = %p allocation = %p \n", (void*) ctx, (void*) id, (void*) ptr));
	fz_free(ctx, ptr);
}

static void*
fz_lcms_realloc(cmsContext id, void *ptr, unsigned int size)
{
	fz_context *ctx = (fz_context *)cmsGetContextUserData(id);
	DEBUG_LCMS_MEM(("Realloc:: mupdf ctx = %p lcms ctx = %p allocation = %p \n", (void*) ctx, (void*) id, (void*) ptr));
	if (ptr == 0)
		return fz_lcms_malloc(id, size);
	if (size == 0)
	{
		fz_lcms_free(id, ptr);
		return NULL;
	}
	return fz_resize_array_no_throw(ctx, ptr, size, 1);
}

static cmsPluginMemHandler fz_lcms_memhandler =
{
	{
		cmsPluginMagicNumber,
		2000,
		cmsPluginMemHandlerSig,
		NULL
	},
	fz_lcms_malloc,
	fz_lcms_free,
	fz_lcms_realloc,
	NULL,
	NULL,
	NULL,
};

static int
fz_lcms_num_devcomps(cmsContext cmm_ctx, fz_iccprofile *profile)
{
	return cmsChannelsOf(cmm_ctx, cmsGetColorSpace(cmm_ctx, profile->cmm_handle));
}

/* Transform pixmap */
void
fz_lcms_transform_pixmap(fz_cmm_instance *instance, fz_icclink *link, fz_pixmap *dst, fz_pixmap *src)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	cmsHTRANSFORM hTransform = (cmsHTRANSFORM)link->cmm_handle;
	fz_context *ctx = (fz_context *)cmsGetContextUserData(cmm_ctx);
	int cmm_num_src, cmm_num_des;
	unsigned char *inputpos, *outputpos;
	int k;
	DEBUG_LCMS_MEM(("@@@@@@@ Transform Pixmap Start:: mupdf ctx = %p lcms ctx = %p link = %p \n", (void*)ctx, (void*)cmm_ctx, (void*)link->cmm_handle));

	/* check the channels. */
	cmm_num_src = T_CHANNELS(cmsGetTransformInputFormat(cmm_ctx, hTransform));
	cmm_num_des = T_CHANNELS(cmsGetTransformOutputFormat(cmm_ctx, hTransform));
	if ((cmm_num_src != (src->n - src->alpha)) || (cmm_num_des != (dst->n - dst->alpha)))
		fz_throw(ctx, FZ_ERROR_GENERIC, "Mismatching color setup in cmm pixmap transformation: src: %d vs %d, dst: %d vs %d", cmm_num_src, src->n, cmm_num_des, dst->n);

	/* Transform */
	inputpos = src->samples;
	outputpos = dst->samples;
	for (k = 0; k < src->h; k++)
	{
		cmsDoTransform(cmm_ctx, hTransform, inputpos, outputpos, src->w);
		inputpos += src->stride;
		outputpos += dst->stride;
	}
	DEBUG_LCMS_MEM(("@@@@@@@ Transform Pixmap End:: mupdf ctx = %p lcms ctx = %p link = %p \n", (void*)ctx, (void*)cmm_ctx, (void*)link->cmm_handle));
}

/* Transform a single color. */
void
fz_lcms_transform_color(fz_cmm_instance *instance, fz_icclink *link, unsigned short *dst, const unsigned short *src)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	cmsHTRANSFORM hTransform = (cmsHTRANSFORM) link->cmm_handle;

	/* Do the conversion */
	cmsDoTransform(cmm_ctx, hTransform, src, dst, 1);
}

void
fz_lcms_new_link(fz_cmm_instance *instance, fz_icclink *link, const fz_color_params *rend, int cmm_flags, int num_bytes, int alpha, const fz_iccprofile *src, const fz_iccprofile *prf, const fz_iccprofile *dst)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	cmsUInt32Number src_data_type, des_data_type;
	cmsColorSpaceSignature src_cs, des_cs;
	int src_num_chan, des_num_chan;
	int lcms_src_cs, lcms_des_cs;
	unsigned int flag = cmsFLAGS_LOWRESPRECALC | cmm_flags;

	DEBUG_LCMS_MEM(("@@@@@@@ Create Link Start:: mupdf ctx = %p lcms ctx = %p src = %p des = %p \n", (void*)ctx, (void*)cmm_ctx, (void*)src->cmm_handle, (void*)dst->cmm_handle));
	/* src */
	src_cs = cmsGetColorSpace(cmm_ctx, src->cmm_handle);
	lcms_src_cs = _cmsLCMScolorSpace(cmm_ctx, src_cs);
	if (lcms_src_cs < 0)
		lcms_src_cs = 0;
	src_num_chan = cmsChannelsOf(cmm_ctx, src_cs);
	src_data_type = (COLORSPACE_SH(lcms_src_cs) | CHANNELS_SH(src_num_chan) | BYTES_SH(num_bytes));
	src_data_type = src_data_type | EXTRA_SH(alpha);

	/* dst */
	des_cs = cmsGetColorSpace(cmm_ctx, dst->cmm_handle);
	lcms_des_cs = _cmsLCMScolorSpace(cmm_ctx, des_cs);
	if (lcms_des_cs < 0)
		lcms_des_cs = 0;
	des_num_chan = cmsChannelsOf(cmm_ctx, des_cs);
	des_data_type = (COLORSPACE_SH(lcms_des_cs) | CHANNELS_SH(des_num_chan) | BYTES_SH(num_bytes));
	des_data_type = des_data_type | EXTRA_SH(alpha);

	/* flags */
	if (rend->bp)
		flag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

	if (alpha)
		flag |= cmsFLAGS_COPY_ALPHA;

	link->depth = num_bytes;
	link->alpha = alpha;

	if (prf == NULL)
	{
		link->cmm_handle = cmsCreateTransformTHR(cmm_ctx, src->cmm_handle, src_data_type, dst->cmm_handle, des_data_type, rend->ri, flag);
		DEBUG_LCMS_MEM(("@@@@@@@ Create Link End:: mupdf ctx = %p lcms ctx = %p link = %p link_cmm = %p src = %p des = %p \n", (void*)ctx, (void*)cmm_ctx, (void*)link, (void*)link->cmm_handle, (void*)src->cmm_handle, (void*)dst->cmm_handle));
	}
	else
	{
		/* littleCMS proof creation links don't work properly with the Ghent
		 * test files. Handle this in a brutish manner.
		 */
		if (src == prf)
		{
			link->cmm_handle = cmsCreateTransformTHR(cmm_ctx, src->cmm_handle, src_data_type, dst->cmm_handle, des_data_type, INTENT_RELATIVE_COLORIMETRIC, flag);
		}
		else if (prf == dst)
		{
			link->cmm_handle = cmsCreateTransformTHR(cmm_ctx, src->cmm_handle, src_data_type, prf->cmm_handle, des_data_type, rend->ri, flag);
		}
		else
		{
			cmsHPROFILE src_to_prf_profile;
			cmsHTRANSFORM src_to_prf_link;
			cmsColorSpaceSignature prf_cs;
			int prf_num_chan;
			int lcms_prf_cs;
			cmsUInt32Number prf_data_type;
			cmsHPROFILE hProfiles[3];

			prf_cs = cmsGetColorSpace(cmm_ctx, prf->cmm_handle);
			lcms_prf_cs = _cmsLCMScolorSpace(cmm_ctx, prf_cs);
			if (lcms_prf_cs < 0)
				lcms_prf_cs = 0;
			prf_num_chan = cmsChannelsOf(cmm_ctx, prf_cs);
			prf_data_type = (COLORSPACE_SH(lcms_prf_cs) | CHANNELS_SH(prf_num_chan) | BYTES_SH(num_bytes));
			src_to_prf_link = cmsCreateTransformTHR(cmm_ctx, src->cmm_handle, src_data_type, prf->cmm_handle, prf_data_type, rend->ri, flag);
			src_to_prf_profile = cmsTransform2DeviceLink(cmm_ctx, src_to_prf_link, 3.4, flag);
			cmsDeleteTransform(cmm_ctx, src_to_prf_link);

			hProfiles[0] = src_to_prf_profile;
			hProfiles[1] = prf->cmm_handle;
			hProfiles[2] = dst->cmm_handle;
			link->cmm_handle = cmsCreateMultiprofileTransformTHR(cmm_ctx, hProfiles, 3, src_data_type, des_data_type, INTENT_RELATIVE_COLORIMETRIC, flag);
			cmsCloseProfile(cmm_ctx, src_to_prf_profile);
		}
	}
}

void
fz_lcms_drop_link(fz_cmm_instance *instance, fz_icclink *link)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	if (link->cmm_handle != NULL)
	{
		DEBUG_LCMS_MEM(("Free Link:: link = %p \n", (void*)link->cmm_handle));
		cmsDeleteTransform(cmm_ctx, link->cmm_handle);
	}
	link->cmm_handle = NULL;
}

static fz_cmm_instance *
fz_lcms_new_instance(fz_context *ctx)
{
	cmsContext cmm_ctx;

	cmm_ctx = cmsCreateContext((void *)&fz_lcms_memhandler, ctx);
	if (cmm_ctx == NULL)
		fz_throw(ctx, FZ_ERROR_GENERIC, "CMM failed to initialize");
	DEBUG_LCMS_MEM(("Context Creation:: mupdf ctx = %p lcms ctx = %p \n", (void*) ctx, (void*) cmm_ctx));
	cmsSetLogErrorHandlerTHR(cmm_ctx, fz_lcms_error);
	return cmm_ctx;
}

static void
fz_lcms_drop_instance(fz_cmm_instance *instance)
{
	DEBUG_LCMS_MEM(("Context Destruction:: lcms ctx = %p \n", (void*)instance));
	if (instance == NULL)
		return;
	cmsDeleteContext(instance);
}

static void
fz_lcms_new_profile(fz_cmm_instance *instance, fz_iccprofile *profile)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	size_t size;
	unsigned char *data;
	fz_context *ctx = (fz_context *)cmsGetContextUserData(cmm_ctx);

	DEBUG_LCMS_MEM(("@@@@@@@ Create Profile Start:: mupdf ctx = %p lcms ctx = %p \n", (void*)ctx, (void*)cmm_ctx));
	cmsSetLogErrorHandlerTHR(cmm_ctx, fz_lcms_error);
	size = fz_buffer_storage(ctx, profile->buffer, &data);
	profile->cmm_handle = cmsOpenProfileFromMemTHR(cmm_ctx, data, size);
	if (profile->cmm_handle != NULL)
		profile->num_devcomp = fz_lcms_num_devcomps(cmm_ctx, profile);
	else
	{
		profile->num_devcomp = 0;
		fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid ICC Profile.");
	}
	DEBUG_LCMS_MEM(("@@@@@@@ Create Profile End:: mupdf ctx = %p lcms ctx = %p profile = %p profile_cmm = %p \n", (void*)ctx, (void*)cmm_ctx, (void*)profile, (void*)profile->cmm_handle));
}

static void
fz_lcms_drop_profile(fz_cmm_instance *instance, fz_iccprofile *profile)
{
	cmsContext cmm_ctx = (cmsContext)instance;
	if (profile->cmm_handle != NULL)
	{
		DEBUG_LCMS_MEM(("Free Profile:: profile = %p \n", (void*) profile->cmm_handle));
		cmsCloseProfile(cmm_ctx, profile->cmm_handle);
	}
	profile->cmm_handle = NULL;
}

fz_cmm_engine fz_cmm_engine_lcms = {
	fz_lcms_new_instance,
	fz_lcms_drop_instance,
	fz_lcms_transform_pixmap,
	fz_lcms_transform_color,
	fz_lcms_new_link,
	fz_lcms_drop_link,
	fz_lcms_new_profile,
	fz_lcms_drop_profile,
	cmsFLAGS_NOWHITEONWHITEFIXUP
};
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Lorenzo Miniero
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Translate between signed linear and Opus (Open Codec)
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 *
 * \note This work was motivated by Mozilla
 *
 * \ingroup codecs
 *
 * \extref The Opus library - http://opus-codec.org
 *
 */

/*** MODULEINFO
	 <depend>opus</depend>
	 <conflict>codec_opus</conflict>
***/

#include "asterisk.h"

#ifdef ASTERISK_REGISTER_FILE
ASTERISK_REGISTER_FILE()
#elif ASTERISK_FILE_VERSION
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")
#endif

#include <opus/opus.h>

#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"

#define	BUFFER_SAMPLES	8000
#define	OPUS_SAMPLES	160

#define USE_FEC		0

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_opus.h"

static struct codec_usage {
	int encoder_id;
	int decoder_id;
	int encoders;
	int decoders;
} usage;

/* Private structures */
struct opus_coder_pvt {
	void *opus;	/* May be encoder or decoder */
	int sampling_rate;
	int multiplier;
	int fec;
	int id;
	int16_t buf[BUFFER_SAMPLES];	/* FIXME */
	int framesize;
};

static int valid_sampling_rate(int rate)
{
	return rate == 8000
		|| rate == 12000
		|| rate == 16000
		|| rate == 24000
		|| rate == 48000;
}

/* Helper methods */
static int opus_encoder_construct(struct ast_trans_pvt *pvt, int sampling_rate)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	int error = 0;

	if (!valid_sampling_rate(sampling_rate)) {
		return -1;
	}

	opvt->sampling_rate = sampling_rate;
	opvt->multiplier = 48000/sampling_rate;
	opvt->fec = USE_FEC;

	opvt->opus = opus_encoder_create(sampling_rate, 1, OPUS_APPLICATION_VOIP, &error);

	if (error != OPUS_OK) {
		ast_log(LOG_ERROR, "Error creating the Opus encoder: %s\n", opus_strerror(error));
		return -1;
	}

	if (sampling_rate == 8000) {
		opus_encoder_ctl(opvt->opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
	} else if (sampling_rate == 12000) {
		opus_encoder_ctl(opvt->opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
	} else if (sampling_rate == 16000) {
		opus_encoder_ctl(opvt->opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
	} else if (sampling_rate == 24000) {
		opus_encoder_ctl(opvt->opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
	} else if (sampling_rate == 48000) {
		opus_encoder_ctl(opvt->opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
	}

	opus_encoder_ctl(opvt->opus, OPUS_SET_INBAND_FEC(opvt->fec));
	opvt->framesize = sampling_rate/50;
	opvt->id = ast_atomic_fetchadd_int(&usage.encoder_id, 1) + 1;

	ast_atomic_fetchadd_int(&usage.encoders, +1);

	ast_debug(3, "Created encoder #%d (%d -> opus)\n", opvt->id, sampling_rate);

	return 0;
}

static int opus_decoder_construct(struct ast_trans_pvt *pvt, int sampling_rate)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	int error = 0;

	if (!valid_sampling_rate(sampling_rate)) {
		return -1;
	}

	opvt->sampling_rate = sampling_rate;
	opvt->multiplier = 48000/sampling_rate;
	opvt->fec = USE_FEC;	/* FIXME: should be triggered by chan_sip */

	opvt->opus = opus_decoder_create(sampling_rate, 1, &error);

	if (error != OPUS_OK) {
		ast_log(LOG_ERROR, "Error creating the Opus decoder: %s\n", opus_strerror(error));
		return -1;
	}

	opvt->id = ast_atomic_fetchadd_int(&usage.decoder_id, 1) + 1;

	ast_atomic_fetchadd_int(&usage.decoders, +1);

	ast_debug(3, "Created decoder #%d (opus -> %d)\n", opvt->id, sampling_rate);

	return 0;
}

/* Translator callbacks */
static int lintoopus_new(struct ast_trans_pvt *pvt)
{
	return opus_encoder_construct(pvt, pvt->t->src_codec.sample_rate);
}

static int opustolin_new(struct ast_trans_pvt *pvt)
{
	return opus_decoder_construct(pvt, pvt->t->dst_codec.sample_rate);
}

static int lintoopus_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct opus_coder_pvt *opvt = pvt->pvt;

	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	memcpy(opvt->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;

	return 0;
}

static struct ast_frame *lintoopus_frameout(struct ast_trans_pvt *pvt)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	while (pvt->samples >= opvt->framesize) {
		/* status is either error or output bytes */
		const int status = opus_encode(opvt->opus,
			opvt->buf + samples,
			opvt->framesize,
			pvt->outbuf.uc,
			BUFFER_SAMPLES);

		ast_debug(3, "[Encoder #%d (%d)] %d samples, %d bytes\n",
				  opvt->id,
				  opvt->sampling_rate,
				  opvt->framesize,
				  opvt->framesize * 2);

		samples += opvt->framesize;
		pvt->samples -= opvt->framesize;

		if (status < 0) {
			ast_log(LOG_ERROR, "Error encoding the Opus frame: %s\n", opus_strerror(status));
		} else {
			struct ast_frame *current = ast_trans_frameout(pvt,
				status,
				opvt->multiplier * opvt->framesize);

			ast_debug(3, "[Encoder #%d (%d)]   >> Got %d samples, %d bytes\n",
					  opvt->id,
					  opvt->sampling_rate,
					  opvt->multiplier * opvt->framesize,
					  status);

			if (!current) {
				continue;
			} else if (last) {
				AST_LIST_NEXT(last, frame_list) = current;
			} else {
				result = current;
			}
			last = current;
		}
	}

	/* Move the data at the end of the buffer to the front */
	if (samples) {
		memmove(opvt->buf, opvt->buf + samples, pvt->samples * 2);
	}

	return result;
}

static int opustolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	int samples = 0;

	/* Decode */
	ast_debug(3, "[Decoder #%d (%d)] %d samples, %d bytes\n",
		opvt->id,
		opvt->sampling_rate,
		f->samples,
		f->datalen);

	if ((samples = opus_decode(opvt->opus, f->data.ptr, f->datalen, pvt->outbuf.i16, BUFFER_SAMPLES, opvt->fec)) < 0) {
		ast_log(LOG_ERROR, "Error decoding the Opus frame: %s\n", opus_strerror(samples));
		return -1;
	}

	pvt->samples += samples;
	pvt->datalen += samples * 2;

	ast_debug(3, "[Decoder #%d (%d)]   >> Got %d samples, %d bytes\n",
		opvt->id,
		opvt->sampling_rate,
		pvt->samples,
		pvt->datalen);

	return 0;
}

static void lintoopus_destroy(struct ast_trans_pvt *arg)
{
	struct opus_coder_pvt *opvt = arg->pvt;

	if (!opvt || !opvt->opus) {
		return;
	}

	opus_encoder_destroy(opvt->opus);
	opvt->opus = NULL;

	ast_atomic_fetchadd_int(&usage.encoders, -1);

	ast_debug(3, "Destroyed encoder #%d (%d->opus)\n", opvt->id, opvt->sampling_rate);
}

static void opustolin_destroy(struct ast_trans_pvt *arg)
{
	struct opus_coder_pvt *opvt = arg->pvt;

	if (!opvt || !opvt->opus) {
		return;
	}

	opus_decoder_destroy(opvt->opus);
	opvt->opus = NULL;

	ast_atomic_fetchadd_int(&usage.decoders, -1);

	ast_debug(3, "Destroyed decoder #%d (opus->%d)\n", opvt->id, opvt->sampling_rate);
}

static char *handle_cli_opus_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct codec_usage copy;

	switch (cmd) {
	case CLI_INIT:
		e->command = "opus show";
		e->usage =
			"Usage: opus show\n"
			"       Displays Opus encoder/decoder utilization.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	copy = usage;

	ast_cli(a->fd, "%d/%d encoders/decoders are in use.\n", copy.encoders, copy.decoders);

	return CLI_SUCCESS;
}

/* Translators */
static struct ast_translator opustolin = {
        .table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP,
        .name = "opustolin",
        .src_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 8000,
        },
        .format = "slin",
        .newpvt = opustolin_new,
        .framein = opustolin_framein,
        .destroy = opustolin_destroy,
        .sample = opus_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintoopus = {
        .table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP,
        .name = "lintoopus",
        .src_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 8000,
        },
        .dst_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "opus",
        .newpvt = lintoopus_new,
        .framein = lintoopus_framein,
        .frameout = lintoopus_frameout,
        .destroy = lintoopus_destroy,
        .sample = slin8_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin12 = {
        .table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP - 1,
        .name = "opustolin12",
        .src_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 12000,
        },
        .format = "slin12",
        .newpvt = opustolin_new,
        .framein = opustolin_framein,
        .destroy = opustolin_destroy,
        .sample = opus_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin12toopus = {
        .table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP - 1,
        .name = "lin12toopus",
        .src_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 12000,
        },
        .dst_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "opus",
        .newpvt = lintoopus_new,
        .framein = lintoopus_framein,
        .frameout = lintoopus_frameout,
        .destroy = lintoopus_destroy,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin16 = {
        .table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP - 2,
        .name = "opustolin16",
        .src_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 16000,
        },
        .format = "slin16",
        .newpvt = opustolin_new,
        .framein = opustolin_framein,
        .destroy = opustolin_destroy,
        .sample = opus_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin16toopus = {
        .table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP - 2,
        .name = "lin16toopus",
        .src_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 16000,
        },
        .dst_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "opus",
        .newpvt = lintoopus_new,
        .framein = lintoopus_framein,
        .frameout = lintoopus_frameout,
        .destroy = lintoopus_destroy,
        .sample = slin16_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin24 = {
        .table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP - 4,
        .name = "opustolin24",
        .src_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 24000,
        },
        .format = "slin24",
        .newpvt = opustolin_new,
        .framein = opustolin_framein,
        .destroy = opustolin_destroy,
        .sample = opus_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin24toopus = {
        .table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP - 4,
        .name = "lin24toopus",
        .src_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 24000,
        },
        .dst_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "opus",
        .newpvt = lintoopus_new,
        .framein = lintoopus_framein,
        .frameout = lintoopus_frameout,
        .destroy = lintoopus_destroy,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin48 = {
        .table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP - 8,
        .name = "opustolin48",
        .src_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "slin48",
        .newpvt = opustolin_new,
        .framein = opustolin_framein,
        .destroy = opustolin_destroy,
        .sample = opus_sample,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin48toopus = {
        .table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP - 8,
        .name = "lin48toopus",
        .src_codec = {
                .name = "slin",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .dst_codec = {
                .name = "opus",
                .type = AST_MEDIA_TYPE_AUDIO,
                .sample_rate = 48000,
        },
        .format = "opus",
        .newpvt = lintoopus_new,
        .framein = lintoopus_framein,
        .frameout = lintoopus_frameout,
        .destroy = lintoopus_destroy,
        .desc_size = sizeof(struct opus_coder_pvt),
        .buffer_samples = BUFFER_SAMPLES,
        .buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_opus_show, "Display Opus codec utilization.")
};

static int reload(void)
{
	/* Reload does nothing */
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&opustolin);
	res |= ast_unregister_translator(&lintoopus);
	res |= ast_unregister_translator(&opustolin12);
	res |= ast_unregister_translator(&lin12toopus);
	res |= ast_unregister_translator(&opustolin16);
	res |= ast_unregister_translator(&lin16toopus);
	res |= ast_unregister_translator(&opustolin24);
	res |= ast_unregister_translator(&lin24toopus);
	res |= ast_unregister_translator(&opustolin48);
	res |= ast_unregister_translator(&lin48toopus);

	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&opustolin);
	res |= ast_register_translator(&lintoopus);
	res |= ast_register_translator(&opustolin12);
	res |= ast_register_translator(&lin12toopus);
	res |= ast_register_translator(&opustolin16);
	res |= ast_register_translator(&lin16toopus);
	res |= ast_register_translator(&opustolin24);
	res |= ast_register_translator(&lin24toopus);
	res |= ast_register_translator(&opustolin48);
	res |= ast_register_translator(&lin48toopus);

	ast_cli_register_multiple(cli, ARRAY_LEN(cli));

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Opus Coder/Decoder",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	);

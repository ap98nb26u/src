/*	$NetBSD: libnvmm.c,v 1.3 2018/11/29 19:55:20 maxv Exp $	*/

/*
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include "nvmm.h"

typedef struct __area {
	LIST_ENTRY(__area) list;
	gpaddr_t gpa;
	uintptr_t hva;
	size_t size;
} area_t;

typedef LIST_HEAD(, __area) area_list_t;

static int nvmm_fd = -1;
static size_t nvmm_page_size = 0;

/* -------------------------------------------------------------------------- */

static int
__area_unmap(struct nvmm_machine *mach, uintptr_t hva, gpaddr_t gpa,
 size_t size)
{
	struct nvmm_ioc_gpa_unmap args;
	int ret;

	args.machid = mach->machid;
	args.gpa = gpa;
	args.size = size;

	ret = ioctl(nvmm_fd, NVMM_IOC_GPA_UNMAP, &args);
	if (ret == -1)
		return -1;

	ret = munmap((void *)hva, size);

	return ret;
}

static int
__area_dig_hole(struct nvmm_machine *mach, uintptr_t hva, gpaddr_t gpa,
    size_t size)
{
	area_list_t *areas = mach->areas;
	area_t *ent, *tmp, *nxt;
	size_t diff;

	LIST_FOREACH_SAFE(ent, areas, list, nxt) {
		/* Case 1. */
		if ((gpa < ent->gpa) && (gpa + size > ent->gpa)) {
			diff = (gpa + size) - ent->gpa;
			if (__area_unmap(mach, ent->hva, ent->gpa, diff) == -1) {
				return -1;
			}
			ent->gpa  += diff;
			ent->hva  += diff;
			ent->size -= diff;
		}

		/* Case 2. */
		if ((gpa >= ent->gpa) && (gpa + size <= ent->gpa + ent->size)) {
			/* First half. */
			tmp = malloc(sizeof(*tmp));
			tmp->gpa = ent->gpa;
			tmp->hva = ent->hva;
			tmp->size = (gpa - ent->gpa);
			LIST_INSERT_BEFORE(ent, tmp, list);
			/* Second half. */
			ent->gpa  += tmp->size;
			ent->hva  += tmp->size;
			ent->size -= tmp->size;
			diff = size;
			if (__area_unmap(mach, ent->hva, ent->gpa, diff) == -1) {
				return -1;
			}
			ent->gpa  += diff;
			ent->hva  += diff;
			ent->size -= diff;
		}

		/* Case 3. */
		if ((gpa < ent->gpa + ent->size) &&
		    (gpa + size > ent->gpa + ent->size)) {
			diff = (ent->gpa + ent->size) - gpa;
			if (__area_unmap(mach, hva, gpa, diff) == -1) {
				return -1;
			}
			ent->size -= diff;
		}

		/* Case 4. */
		if ((gpa < ent->gpa + ent->size) &&
		    (gpa + size > ent->gpa + ent->size)) {
			if (__area_unmap(mach, ent->hva, ent->gpa, ent->size) == -1) {
				return -1;
			}
			LIST_REMOVE(ent, list);
			free(ent);
		}
	}

	return 0;
}

static int
__area_add(struct nvmm_machine *mach, uintptr_t hva, gpaddr_t gpa, size_t size)
{
	area_list_t *areas = mach->areas;
	area_t *area;
	int ret;

	area = malloc(sizeof(*area));
	if (area == NULL)
		return -1;
	area->gpa = gpa;
	area->hva = hva;
	area->size = size;

	ret = __area_dig_hole(mach, hva, gpa, size);
	if (ret == -1) {
		free(area);
		return -1;
	}

	LIST_INSERT_HEAD(areas, area, list);
	return 0;
}

static void
__area_remove_all(struct nvmm_machine *mach)
{
	area_list_t *areas = mach->areas;
	area_t *ent;

	while ((ent = LIST_FIRST(areas)) != NULL) {
		LIST_REMOVE(ent, list);
		free(ent);
	}

	free(areas);
}

/* -------------------------------------------------------------------------- */

static int
nvmm_init(void)
{
	if (nvmm_fd != -1)
		return 0;
	nvmm_fd = open("/dev/nvmm", O_RDWR);
	if (nvmm_fd == -1)
		return -1;
	nvmm_page_size = sysconf(_SC_PAGESIZE);
	return 0;
}

int
nvmm_capability(struct nvmm_capability *cap)
{
	struct nvmm_ioc_capability args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	ret = ioctl(nvmm_fd, NVMM_IOC_CAPABILITY, &args);
	if (ret == -1)
		return -1;

	memcpy(cap, &args.cap, sizeof(args.cap));

	return 0;
}

int
nvmm_machine_create(struct nvmm_machine *mach)
{
	struct nvmm_ioc_machine_create args;
	area_list_t *areas;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	areas = calloc(1, sizeof(*areas));
	if (areas == NULL)
		return -1;

	ret = ioctl(nvmm_fd, NVMM_IOC_MACHINE_CREATE, &args);
	if (ret == -1) {
		free(areas);
		return -1;
	}

	memset(mach, 0, sizeof(*mach));
	LIST_INIT(areas);
	mach->areas = areas;
	mach->machid = args.machid;

	return 0;
}

int
nvmm_machine_destroy(struct nvmm_machine *mach)
{
	struct nvmm_ioc_machine_destroy args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;

	ret = ioctl(nvmm_fd, NVMM_IOC_MACHINE_DESTROY, &args);
	if (ret == -1)
		return -1;

	__area_remove_all(mach);

	return 0;
}

int
nvmm_machine_configure(struct nvmm_machine *mach, uint64_t op, void *conf)
{
	struct nvmm_ioc_machine_configure args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.op = op;
	args.conf = conf;

	ret = ioctl(nvmm_fd, NVMM_IOC_MACHINE_CONFIGURE, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_create(struct nvmm_machine *mach, nvmm_cpuid_t cpuid)
{
	struct nvmm_ioc_vcpu_create args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_CREATE, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_destroy(struct nvmm_machine *mach, nvmm_cpuid_t cpuid)
{
	struct nvmm_ioc_vcpu_destroy args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_DESTROY, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_setstate(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    void *state, uint64_t flags)
{
	struct nvmm_ioc_vcpu_setstate args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;
	args.state = state;
	args.flags = flags;

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_SETSTATE, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_getstate(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    void *state, uint64_t flags)
{
	struct nvmm_ioc_vcpu_getstate args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;
	args.state = state;
	args.flags = flags;

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_GETSTATE, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_inject(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    struct nvmm_event *event)
{
	struct nvmm_ioc_vcpu_inject args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;
	memcpy(&args.event, event, sizeof(args.event));

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_INJECT, &args);
	if (ret == -1)
		return -1;

	return 0;
}

int
nvmm_vcpu_run(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    struct nvmm_exit *exit)
{
	struct nvmm_ioc_vcpu_run args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	args.machid = mach->machid;
	args.cpuid = cpuid;
	memset(&args.exit, 0, sizeof(args.exit));

	ret = ioctl(nvmm_fd, NVMM_IOC_VCPU_RUN, &args);
	if (ret == -1)
		return -1;

	memcpy(exit, &args.exit, sizeof(args.exit));

	return 0;
}

int
nvmm_gpa_map(struct nvmm_machine *mach, uintptr_t hva, gpaddr_t gpa,
    size_t size, int flags)
{
	struct nvmm_ioc_gpa_map args;
	int ret;

	if (nvmm_init() == -1) {
		return -1;
	}

	ret = __area_add(mach, hva, gpa, size);
	if (ret == -1)
		return -1;

	args.machid = mach->machid;
	args.hva = hva;
	args.gpa = gpa;
	args.size = size;
	args.flags = flags;

	ret = ioctl(nvmm_fd, NVMM_IOC_GPA_MAP, &args);
	if (ret == -1) {
		/* Can't recover. */
		abort();
	}

	return 0;
}

int
nvmm_gpa_unmap(struct nvmm_machine *mach, uintptr_t hva, gpaddr_t gpa,
    size_t size)
{
	if (nvmm_init() == -1) {
		return -1;
	}

	return __area_dig_hole(mach, hva, gpa, size);
}

/*
 * nvmm_gva_to_gpa(): architecture-specific.
 */

int
nvmm_gpa_to_hva(struct nvmm_machine *mach, gpaddr_t gpa, uintptr_t *hva)
{
	area_list_t *areas = mach->areas;
	area_t *ent;

	if (gpa % nvmm_page_size != 0) {
		errno = EINVAL;
		return -1;
	}

	LIST_FOREACH(ent, areas, list) {
		if (gpa < ent->gpa) {
			continue;
		}
		if (gpa >= ent->gpa + ent->size) {
			continue;
		}

		*hva = ent->hva + (gpa - ent->gpa);
		return 0;
	}

	errno = ENOENT;
	return -1;
}

/*
 * nvmm_assist_io(): architecture-specific.
 */

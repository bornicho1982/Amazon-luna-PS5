// SPDX-License-Identifier: GPL-3.0-or-later
/* Stub de enlace para libSceVideodec2.sprx en PlayStation 5 */
int sceVideodec2QueryComputeMemoryInfo(void *info) { (void)info; return -1; }
int sceVideodec2AllocateComputeQueue(const void *cfg, const void *mem, void **queue) { (void)cfg; (void)mem; (void)queue; return -1; }
int sceVideodec2ReleaseComputeQueue(void *queue) { (void)queue; return -1; }
int sceVideodec2QueryDecoderMemoryInfo(const void *cfg, void *mem) { (void)cfg; (void)mem; return -1; }
int sceVideodec2CreateDecoder(const void *cfg, const void *mem, void **decoder) { (void)cfg; (void)mem; (void)decoder; return -1; }
int sceVideodec2DeleteDecoder(void *decoder) { (void)decoder; return -1; }
int sceVideodec2Decode(void *decoder, const void *in, void *fb, void *out) { (void)decoder; (void)in; (void)fb; (void)out; return -1; }
int sceVideodec2Flush(void *decoder, void *fb, void *out) { (void)decoder; (void)fb; (void)out; return -1; }
int sceVideodec2Reset(void *decoder) { (void)decoder; return -1; }

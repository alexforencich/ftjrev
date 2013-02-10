/*
 * (c) 2007 Stanislaw K Skowronek
 * Licensed under the terms of GPLv2 or later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <ftdi.h>

#define MAX_IR 512
#define MAX_DEV 80

// clock detection - number of samples and threshold
#define CLKPASS 64
#define CLKTHR 2

// connection detection - number of samples and denoising
#define CONPASS 4
#define CONMATCH 4

struct ftdi_context ctx;

int total_ir;
int ndev;
int total_bsc;
int bypass[MAX_DEV];
char *name[MAX_DEV];
int irlen[MAX_DEV];
uint32_t sample[MAX_DEV];
uint32_t extest[MAX_DEV];
int bslen[MAX_DEV];
char **bsname[MAX_DEV];
char *bsmode[MAX_DEV];
int *bszpin[MAX_DEV];
int *bszval[MAX_DEV];
int *bsextr[MAX_DEV];

int *bsc_in_global;
int *bsc_out_global;
int *bsc_in[MAX_DEV];
int *bsc_out[MAX_DEV];

int *bsc_clk[MAX_DEV];
int *bsc_tmp[MAX_DEV];
int *bsc_sig0[MAX_DEV];
int *bsc_sig1[MAX_DEV];

void setgpio(uint8_t val)
{
	uint8_t buf[3];
	buf[0] = SET_BITS_LOW;
	buf[1] = 0x08 | (val<<4);
	buf[2] = 0xDB;
	if(ftdi_write_data(&ctx, buf, 3)<0)
		fprintf(stderr, "write error\n");
}

void setspeed(uint16_t val)
{
	uint8_t buf[3];
	buf[0] = TCK_DIVISOR;
	buf[1] = val;
	buf[2] = val>>8;
	if(ftdi_write_data(&ctx, buf, 3)<0)
		fprintf(stderr, "write error\n");
}

uint8_t getstat(void)
{
	uint8_t res;
	if(ftdi_read_pins(&ctx, &res)<0)
		fprintf(stderr, "read error\n");
	return res;
}

int open_cable(void)
{
	if(!ftdi_usb_open(&ctx, 0x8482, 0x1002))
		return 0;
	if(!ftdi_usb_open(&ctx, 0x0403, 0x6010))
		return 0;
	return 1;
}

int init(void)
{
	uint8_t latency;
	ftdi_init(&ctx);
	ftdi_set_interface(&ctx, INTERFACE_A);
	if(open_cable())
		return 1;
	ftdi_usb_reset(&ctx);
	ftdi_set_latency_timer(&ctx, 1);
	ftdi_get_latency_timer(&ctx, &latency);
	if(latency != 1)
		fprintf(stderr, "fail latency set\n");
	if(ftdi_set_bitmode(&ctx, 0x0B, BITMODE_MPSSE))
		return 1;
	setspeed(1);
	setgpio(9);
	ftdi_usb_purge_buffers(&ctx);
	return 0;
}

void done(void)
{
	setgpio(5);
	ftdi_usb_close(&ctx);
	ftdi_deinit(&ctx);
}

uint8_t last;
void clock(int tms, int tdi)
{
	uint8_t buf[3];
	int r, t=0;
	buf[0] = MPSSE_WRITE_TMS|MPSSE_DO_READ|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
	buf[1] = 0x00;
	buf[2] = (tdi<<7)|tms;
	if(ftdi_write_data(&ctx, buf, 3)<0)
		fprintf(stderr, "write error\n");
	do {
		if((r=ftdi_read_data(&ctx, buf, 1))<0)
			fprintf(stderr, "read error\n");
		if(t>5) {
			fprintf(stderr, "read timeout\n");
			break;
		}
		t++;
	} while(r!=1);
	last = buf[0]>>7;
}

int gettdo(void)
{
	return last;
}

uint32_t shifti(uint32_t v, int l, int t)
{
	uint8_t buf[16];
	uint32_t p=0, i, ol = l, ib = 0, to = 0, r;
	// prepare and send TDO/TMS message
	if((l-t)>=8) {
		buf[0] = MPSSE_DO_WRITE|MPSSE_DO_READ|MPSSE_LSB|MPSSE_WRITE_NEG;
		buf[1] = ((l-t)/8)-1;
		buf[2] = 0x00;
		p = 3;
		for(i=0;i<=buf[1];i++) {
			buf[p] = v;
			v >>= 8;
			l -= 8;
			p++;
			ib++;
		}
	}
	if((l-t)>=1) {
		buf[p] = MPSSE_DO_WRITE|MPSSE_DO_READ|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
		buf[p+1] = l-t-1;
		buf[p+2] = v;
		v >>= l-t;
		l = t;
		p += 3;
		ib++;
	}
	if(l) {
		buf[p] = MPSSE_WRITE_TMS|MPSSE_DO_READ|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
		buf[p+1] = 0x00;
		buf[p+2] = (v<<7)|1;
		p += 3;
		ib++;
	}
	if(ftdi_write_data(&ctx, buf, p)<0)
		fprintf(stderr, "write error\n");
	// read and unpack TDI message
	p = 0;
	do {
		if((r=ftdi_read_data(&ctx, buf+p, ib-p))<0) {
			fprintf(stderr, "read error\n");
			return 0;
		}
		p += r;
		if(to>5) {
			fprintf(stderr, "read timeout\n");
			return 0;
		}
		to++;
	} while(p<ib);
	p = 0;
	l = ol;
	r = 0;
	while((l-t)>=8) {
		r |= buf[p] << (p*8);
		l -= 8;
		p++;
	}
	if((l-t) >= 1) {
		r |= (buf[p]>>(8-(l-t))) << (p*8);
		p++;
	}
	if(t)
		r |= (buf[p]>>7) << (ol-1);
	return r;
}

void int_shiftr(uint32_t state, int *out, int *in, int l, int t)
{
	uint8_t buf[8192], fl=in?MPSSE_DO_READ:0;
	uint32_t p=0, i, ol = l, ib = 0, to = 0, r, vp = 0, j, a, vl;
	// prepare and send TDO/TMS message
	if(state) {
		buf[p++] = MPSSE_WRITE_TMS|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
		buf[p++] = (state>>8)-1;
		buf[p++] = state;
	}
	if((l-t)>=8) {
		buf[p++] = MPSSE_DO_WRITE|fl|MPSSE_LSB|MPSSE_WRITE_NEG;
		vl = ((l-t)/8)-1;
		buf[p++] = vl;
		buf[p++] = vl>>8;
		for(i=0;i<=vl;i++) {
			a = 0;
			for(j=0;j<8;j++)
				a |= (out[vp+j])<<j;
			buf[p] = a;
			vp += 8;
			l -= 8;
			p++;
			ib++;
		}
	}
	if((l-t)>=1) {
		buf[p] = MPSSE_DO_WRITE|fl|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
		buf[p+1] = l-t-1;
		a = 0;
		for(j=0;j<l-t;j++)
			a |= (out[vp+j])<<j;
		buf[p+2] = a;
		vp += l-t;
		l = t;
		p += 3;
		ib++;
	}
	if(l) {
		buf[p] = MPSSE_WRITE_TMS|fl|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
		buf[p+1] = 0x05;
		buf[p+2] = (out[vp]<<7)|0x03;
		p += 3;
		ib++;
	}
	if(in)
		buf[p++] = SEND_IMMEDIATE;
	if(ftdi_write_data(&ctx, buf, p)<0)
		fprintf(stderr, "write error (%s)\n", ftdi_get_error_string(&ctx));
	// skip if nothing to be read
	if(!in)
		return;
	// read and unpack TDI message
	p = 0;
	do {
		if((r=ftdi_read_data(&ctx, buf+p, ib-p))<0) {
			fprintf(stderr, "read error\n");
			return;
		}
		p += r;
		if(to>10) {
			fprintf(stderr, "read timeout\n");
			return;
		}
		to++;
	} while(p<ib);
	p = 0;
	l = ol;
	vp = 0;
	while((l-t)>=8) {
		for(j=0;j<8;j++)
			in[vp+j] = (buf[p]>>j)&1;
		vp += 8;
		l -= 8;
		p++;
	}
	if((l-t) >= 1) {
		for(j=0;j<l-t;j++)
			in[vp+j] = (buf[p]>>(j+8-(l-t)))&1;
		vp += l-t;
		p++;
	}
	if(t)
		in[vp] = buf[p]>>7;
}

#define MAXBITS 2048
void shiftr(uint32_t state, int *out, int *in, int l, int t)
{
	int p=0, b;
	while(l>0) {
		b=l;
		if(b>MAXBITS)
			b=MAXBITS;
		int_shiftr(state, out+p, in?in+p:NULL, b, (b==l)?t:0);
		l-=b;
		p+=b;
		state=0;
	}
}

void statewalk(uint8_t tms, int l, int tdi)
{
	uint8_t buf[3];
	buf[0] = MPSSE_WRITE_TMS|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
	buf[1] = l-1;
	buf[2] = (tdi<<7)|tms;
	if(ftdi_write_data(&ctx, buf, 3)<0)
		fprintf(stderr, "write error\n");
}

void reset(void)
{
	statewalk(0x1F, 6, 0);
}

void cap_dr(void)
{
	statewalk(0x01, 2, 0);
}

void cap_ir(void)
{
	statewalk(0x03, 3, 0);
}

int regsize(int max)
{
	int i;
	int *r1 = malloc(max*sizeof(int));
	int *r2 = malloc(max*sizeof(int));
	for(i=0;i<max;i++)
		r1[i] = 0xFFFFFFFF;
	clock(0,0);
	shiftr(0, r1, r2, max, 0);
	clock(0,0);
	shiftr(0, r1, r2, max, 0);
	statewalk(0x03, 3, 1);
	for(i=0;i<max;i++) {
		if(!r2[i]) {
			free(r1);
			free(r2);
			return i+1;
		}
	}
	reset();
	free(r1);
	free(r2);
	return -1;
}

int size_chain(void)
{
	cap_ir();
	total_ir=regsize(MAX_IR);
	if(total_ir==-1)
		return 1;
	cap_dr();
	ndev=regsize(MAX_DEV);
	if(ndev==-1)
		return 1;
	return 0;
}

void loadinfo(int i, uint32_t b)
{
	char fname[32];
	char line[256], *p, *q;
	int idx;
	FILE *f;
	if(b!=0)
	    sprintf(fname, "dev/%08X", b);
	else
	    sprintf(fname, "local/%d", i);
	f=fopen(fname,"r");
	if(!f) {
		name[i]=NULL;
		return;
	}
	bypass[i]=7;
	while(fgets(line, 256, f)) {
		p=strpbrk(line, "\n\r");
		if(p)
			*p=0;
		p=strpbrk(line, "\t ");
		if(!p) {
			fprintf(stderr, "%s: malformed line '%s'\n", fname, line);
			name[i]=NULL;
			return;
		}
		*p=0;
		p++;
		while(*p=='\t' || *p==' ')
			p++;
		if(!strcmp(line,"name")) {
			name[i]=strdup(p);
		} else if(!strcmp(line,"irsize")) {
			irlen[i]=strtol(p,NULL,0);
		} else if(!strcmp(line,"sample")) {
			sample[i]=strtol(p,NULL,2);
			bypass[i]&=~1;
		} else if(!strcmp(line,"extest")) {
			extest[i]=strtol(p,NULL,2);
			bypass[i]&=~2;
		} else if(!strcmp(line,"bssize")) {
			bslen[i]=strtol(p,NULL,0);
			bypass[i]&=~4;
			bsname[i]=calloc(sizeof(char *), bslen[i]);
			bsmode[i]=calloc(sizeof(char), bslen[i]);
			bszpin[i]=calloc(sizeof(int), bslen[i]);
			bszval[i]=calloc(sizeof(int), bslen[i]);
			bsextr[i]=calloc(sizeof(int), bslen[i]);
		} else if(!strncmp(line,"bsc[",4) || !strncmp(line,"ext[",4)) {
			idx=strtol(line+4,&q,0);
			if(!q || *q!=']' || idx<0 || idx>bslen[i]) {
				fprintf(stderr, "%s: bad index in '%s'\n", fname, line);
				name[i]=NULL;
				return;
			}
			q=strchr(p,' ');
			*(q++)=0;
			if(!strncmp(line,"ext[",4))
				bsextr[i][idx]=1;
			bsname[i][idx]=strdup(p);
			bsmode[i][idx]=*q;
			if(*q=='Z' || *q=='Y') {
				bszval[i][idx]=q[1]-'0';
				bszpin[i][idx]=strtol(q+3, &p, 0);
				if(!p || *p) {
					fprintf(stderr, "%s: bad tristate pin in '%s'\n", fname, line);
					name[i]=NULL;
					return;
				}
			}
		} else {
			fprintf(stderr, "%s: unknown field '%s'\n", fname, line);
			name[i]=NULL;
			return;
		}
	}
	fclose(f);
}

int idcodes(void)
{
	int i,fail=0,irsum=0,alt=0;
	uint32_t b;
	reset();
	statewalk(0x01, 3, 0); // cap_dr + '0'
	for(i=0;i<ndev;i++) {
		clock(0,alt);
		alt^=1;
		b=gettdo();
		if(b)
			b|=shifti(0x55555555<<alt, 31, 0)<<1;
		loadinfo(i, 0);
		if(!name[i])
		    loadinfo(i, b);
		if(name[i]) {
			fprintf(stderr, "Device %d: IDCODE %08X (%s)\n", i, b, name[i]);
			if(!irlen[i]) {
				fprintf(stderr, "  Missing IR length\n");
				fail=1;
			}
			if(bypass[i]) {
				fprintf(stderr, "  Device is bypassed\n");
				total_bsc++;
			} else
				total_bsc += bslen[i];
			irsum+=irlen[i];
		} else {
			fprintf(stderr, "Device %d: IDCODE %08X (MANUF: %03X, PART: %04X, VER: %01X)\n", i, b, (b>>1)&0x3FF, (b>>12)&0xFFFF, (b>>28)&0xF);
			fail=1;
		}
	}
	b = 0;
	bsc_in_global=calloc(sizeof(int), total_bsc);
	bsc_out_global=calloc(sizeof(int), total_bsc);
	for(i=0;i<ndev;i++) {
		bsc_in[i]=bsc_in_global+b;
		bsc_out[i]=bsc_out_global+b;
		if(bypass[i])
			b++;
		else
			b += bslen[i];
	}
	fprintf(stderr, "Total boundary scan chain: %d\n", total_bsc);
	statewalk(0x03, 3, 1);
	if(irsum!=total_ir) {
		fprintf(stderr, "Total IR length %d mismatches sum of device IR lengths %d\n", total_ir, irsum);
		fail=1;
	}
	return fail;
}

void set_ir_all(uint32_t *ir)
{
	int i, ir_val[MAX_IR], p=0, j;
	for(i=0;i<ndev;i++) {
		if(bypass[i] || !ir) {
			for(j=0; j<irlen[i]; j++)
				ir_val[p++] = 1;
		} else {
			for(j=0; j<irlen[i]; j++)
				ir_val[p++] = (ir[i]>>j)&1;
		}
	}
	shiftr(0x403, ir_val, NULL, p, 1);
}

void bsc_to_allz(void)
{
	int i,j;
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			for(j=0;j<bslen[i];j++)
				bsc_out[i][j]=0;
			for(j=0;j<bslen[i];j++)
				if(bsmode[i][j]=='Y' || bsmode[i][j]=='Z')
					bsc_out[i][bszpin[i][j]]=bszval[i][j];
		}
}

void shift_bsc(int do_in)
{
	if(do_in)
		shiftr(0x301, bsc_out_global, bsc_in_global, total_bsc, 1);
	else
		shiftr(0x301, bsc_out_global, NULL, total_bsc, 1);
}

void print_bsc(int **bsc)
{
	int i,j,h;
	for(i=0;i<ndev;i++) {
		if(bypass[i]) {
			printf("%d: BYPASS\n", i);
		} else {
			printf("%d: ", i);
			h=0;
			for(j=0;j<bslen[i];j++)
				if(bsname[i][j]) {
					if(h)
						printf("   ");
					switch(bsmode[i][j]) {
					case 'Y':
					case 'Z':
						if(bsc[i][bszpin[i][j]]==bszval[i][j])
							printf("%s out: Z\n", bsname[i][j]);
						else
							printf("%s out: %d\n", bsname[i][j], bsc[i][j]);
						break;
					default:
						printf("%s: %d\n", bsname[i][j], bsc[i][j]);
					}
					h=1;
				}
		}
	}
}

void find_clocks(void)
{
	int i,j,p;
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			bsc_clk[i]=calloc(sizeof(int),bslen[i]);
			bsc_tmp[i]=calloc(sizeof(int),bslen[i]);
		}
	bsc_to_allz();
	set_ir_all(sample);
	shift_bsc(0);
	for(p=0;p<CLKPASS;p++) {
		set_ir_all(extest);
		shift_bsc(1);
		set_ir_all(sample);
		if(p)
			for(i=0;i<ndev;i++)
				if(!bypass[i])
					for(j=0;j<bslen[i];j++)
						if(bsc_in[i][j])
							bsc_tmp[i][j]++;
	}
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			for(j=0;j<bslen[i];j++) {
				bsc_clk[i][j]=(bsc_tmp[i][j]>=CLKTHR && bsc_tmp[i][j]<=(CLKPASS-CLKTHR))?1:0;
				if(bsc_clk[i][j]) {
					if(bsname[i][j])
						printf("CLOCK: %d[%s]:%s\n", i, name[i], bsname[i][j]);
					else
						printf("CLOCK: %d[%s]:bsc[%d]\n", i, name[i], j);
				}
			}
			free(bsc_tmp[i]);
		}
}

void find_receiver(int ci, int cj)
{
	int i,j,p;
//fprintf(stderr,"    Pin %d[%s]:%s\n", ci, name[ci], bsname[ci][cj]);
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			bsc_sig0[i]=calloc(sizeof(int),bslen[i]);
			bsc_sig1[i]=calloc(sizeof(int),bslen[i]);
		}
	reset();
	bsc_to_allz();
	if(bsmode[ci][cj]=='Y' || bsmode[ci][cj]=='Z')
		bsc_out[ci][bszpin[ci][cj]]=!bszval[ci][cj];
	bsc_out[ci][cj]=0;
	set_ir_all(sample);
	shift_bsc(0);
	for(p=0;p<CONPASS;p++) {
		bsc_out[ci][cj]=!(p&1);
		set_ir_all(extest);
		shift_bsc(1);
		set_ir_all(sample);
		for(i=0;i<ndev;i++)
			if(!bypass[i])
				for(j=0;j<bslen[i];j++) {
					if(bsc_in[i][j] == (p&1))
						bsc_sig0[i][j]++;
					if(bsc_in[i][j] != (p&1))
						bsc_sig1[i][j]++;
				}
	}
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			for(j=0;j<bslen[i];j++) {
				if(!bsc_clk[i][j] && bsc_sig0[i][j]>=CONMATCH) {
					if(bsname[i][j]) {
						if(strcmp(bsname[i][j], bsname[ci][cj]))
							printf("%d[%s]:%s --> %d[%s]:%s\n", ci, name[ci], bsname[ci][cj], i, name[i], bsname[i][j]);
					} else
						printf("%d[%s]:%s --> %d[%s]:bsc[%d]\n", ci, name[ci], bsname[ci][cj], i, name[i], j);
				}
				if(!bsc_clk[i][j] && bsc_sig1[i][j]>=CONMATCH) {
					if(bsname[i][j]) {
						if(strcmp(bsname[i][j], bsname[ci][cj]))
							printf("%d[%s]:%s #-> %d[%s]:%s\n", ci, name[ci], bsname[ci][cj], i, name[i], bsname[i][j]);
					} else
						printf("%d[%s]:%s #-> %d[%s]:bsc[%d]\n", ci, name[ci], bsname[ci][cj], i, name[i], j);
				}
			}
			free(bsc_sig0[i]);
			free(bsc_sig1[i]);
		}
	fflush(stdout);
}

void find_all_pins(void)
{
	int i,j;
	for(i=0;i<ndev;i++)
		if(!bypass[i])
			for(j=0;j<bslen[i];j++)
				if(!bsc_clk[i][j] && !bsextr[i][j] && bsname[i][j] && strchr("OYZ",bsmode[i][j]))
					find_receiver(i, j);
}

void probe_output(int ci, int cj)
{
	reset();
	bsc_to_allz();
	if(bsmode[ci][cj]=='Y' || bsmode[ci][cj]=='Z')
		bsc_out[ci][bszpin[ci][cj]]=!bszval[ci][cj];
	bsc_out[ci][cj]=0;
	set_ir_all(sample);
	shift_bsc(0);
	printf("%d:%s\n", ci, bsname[ci][cj]);
	fflush(stdout);
	bsc_out[ci][cj]=1;
	set_ir_all(extest);
	shift_bsc(0);
	bsc_out[ci][cj]=0;
	set_ir_all(extest);
}

void probe_outputs(void)
{
	int i,j;
	for(i=0;i<ndev;i++)
		if(!bypass[i])
			for(j=0;j<bslen[i];j++)
				if(!bsc_clk[i][j] && !bsextr[i][j] && bsname[i][j] && strchr("OYZ",bsmode[i][j]))
					probe_output(i, j);
}

void probe_inputs()
{
	int i,j,p;
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			bsc_sig0[i]=calloc(sizeof(int),bslen[i]);
			bsc_sig1[i]=calloc(sizeof(int),bslen[i]);
		}
	reset();
	bsc_to_allz();
	set_ir_all(sample);
	shift_bsc(0);
	for(p=0;p<CONPASS;p++) {
		//bsc_out[ci][cj]=!(p&1);
		// toggle pin
		if(p&1)
		    setgpio(9);
		else
		    setgpio(1);
		shift_bsc(1);
		set_ir_all(sample);
		for(i=0;i<ndev;i++)
			if(!bypass[i])
				for(j=0;j<bslen[i];j++) {
					if(bsc_in[i][j] == (p&1))
						bsc_sig0[i][j]++;
					if(bsc_in[i][j] != (p&1))
						bsc_sig1[i][j]++;
				}
	}
	for(i=0;i<ndev;i++)
		if(!bypass[i]) {
			for(j=0;j<bslen[i];j++) {
				if(!bsc_clk[i][j] && bsc_sig0[i][j]>=CONMATCH) {
					if(bsname[i][j]) {
						printf("%d[%s]:%s\n", i, name[i], bsname[i][j]);
					} else
						printf("%d[%s]:bsc[%d]\n", i, name[i], j);
				}
				if(!bsc_clk[i][j] && bsc_sig1[i][j]>=CONMATCH) {
					if(bsname[i][j]) {
						printf("!%d[%s]:%s\n", i, name[i], bsname[i][j]);
					} else
						printf("!%d[%s]:bsc[%d]\n", i, name[i], j);
				}
			}
			free(bsc_sig0[i]);
			free(bsc_sig1[i]);
		}
	fflush(stdout);
}

void signal_handler(int sig)
{
	reset();
	done();
	exit(sig<<8);
}

void usage(void)
{
	fprintf(stderr, "Usage: ftjrev command\n");
	fprintf(stderr, "    init     initialize chan and exit\n");
	fprintf(stderr, "    scan     scan for connections\n");
	fprintf(stderr, "    clocks   scan for clocks only\n");
	fprintf(stderr, "    iprobe   probe inputs with JTAG GPIO pin\n");
	fprintf(stderr, "    oprobe   probe outputs with oscilloscope\n");
}

int main(int argc, char *argv[])
{
	int do_scan_clock = 0;
	int do_scan_chain = 0;
	int do_iprobe = 0;
	int do_oprobe = 0;
	if(argc != 2) {
		usage();
		return 0;
	}
	if(!strcmp(argv[1], "init")){
		// good to go
	} else if(!strcmp(argv[1], "scan")){
		do_scan_clock = 1;
		do_scan_chain = 1;
	} else if(!strcmp(argv[1], "clocks")){
		do_scan_clock = 1;
	} else if(!strcmp(argv[1], "iprobe")){
		do_scan_clock = 1;
		do_iprobe = 1;
	} else if(!strcmp(argv[1], "oprobe")){
		do_scan_clock = 1;
		do_oprobe = 1;
	} else {
		usage();
		return 0;
	}
	if(init()) {
		fprintf(stderr, "Cable not found\n");
		return 1;
	}
	signal(SIGINT, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
	reset();
	if(size_chain()) {
		fprintf(stderr, "Chain sizing failed\n");
		reset();
		done();
		return 1;
	}
	fprintf(stderr, "Found %d devices with total IR length of %d\n", ndev, total_ir);
	if(idcodes()) {
		fprintf(stderr, "Missing device information\n");
		reset();
		done();
		return 1;
	}
	reset();
	if(do_scan_clock) {
		fprintf(stderr, "Clock pass...\n");
		find_clocks();
		reset();
		fflush(stdout);
	}
	if(do_scan_chain) {
		fprintf(stderr, "Pin pass...\n");
		find_all_pins();
		reset();
	}
	if(do_iprobe) {
		fprintf(stderr, "Probing inputs, press ctrl+c to stop...\n");
		
		while (1) {
			probe_inputs();
		}
		
		reset();
	}
	if(do_oprobe) {
		fprintf(stderr, "Probing outputs, press ctrl+c to stop...\n");
		
		while (1) {
			probe_outputs();
		}
		
		reset();
	}
	done();
	return 0;
}

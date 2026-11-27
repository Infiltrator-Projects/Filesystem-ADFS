/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <infiltratr/filesystem_module_v2.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum{B=512,N=256,MAX=B*N};
static uint8_t disk[MAX],saved[MAX];static unsigned big,sector,allocs,fail;
static uint8_t*blk(unsigned b){return disk+b*B;}
static void p16(uint8_t*p,uint32_t n){p[0]=n;p[1]=n>>8;}
static void p24(uint8_t*p,uint32_t n){p16(p,n);p[2]=n>>16;}
static void p32(uint8_t*p,uint32_t n){p16(p,n);p16(p+2,n>>16);}
static uint32_t g32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void bits(uint8_t*p,unsigned at,unsigned count,unsigned value)
{for(unsigned i=0;i<count;++i){unsigned bit=1u<<((at+i)%8);if(value&(1u<<i))p[(at+i)/8]|=bit;else p[(at+i)/8]&=~bit;}}
static void frag(unsigned at,unsigned len,unsigned id){bits(disk,at,12,id);bits(disk,at+len-1,1,1);}
static void zonecheck(void)
{
    uint32_t a=0,b=0,c=0,d=0;
    for(int i=B-4;i>0;i-=4){
        a+=disk[i]+(d>>8);d&=255;b+=disk[i+1]+(a>>8);a&=255;
        c+=disk[i+2]+(b>>8);b&=255;d+=disk[i+3]+(c>>8);c&=255;
    }
    a+=d>>8;b+=disk[1]+(a>>8);c+=disk[2]+(b>>8);d+=disk[3]+(c>>8);
    disk[0]=(uint8_t)(a^b^c^d);
}
static uint32_t rot(uint32_t x){return x>>13|x<<19;}
static uint8_t fold(uint32_t x){return (uint8_t)(x^(x>>8)^(x>>16)^(x>>24));}
static void entry(uint8_t*p,const char*name,uint32_t id,uint32_t bytes,unsigned dir)
{memcpy(p,name,strlen(name));p32(p+18,bytes);p24(p+22,id);p[25]=dir?8:1;}
static void bigentry(uint8_t*p,unsigned at,const char*name,uint32_t id,uint32_t bytes,unsigned dir,unsigned ptr)
{p32(p+at+8,bytes);p32(p+at+12,id);p32(p+at+16,dir?8:1);p32(p+at+20,strlen(name));p32(p+at+24,ptr);}
static void dir(unsigned b,unsigned parent,unsigned root)
{
    uint8_t*p=blk(b);unsigned count=root?2:1;uint32_t c=0;
    if(!big){
        p[0]=7;memcpy(p+1,"Nick",4);p[2042]=7;memcpy(p+2043,"Nick",4);p24(p+2010,parent);
        if(root){entry(p+5,"PROOF/BIN",0x300,16*B+17,0);entry(p+31,"DIR",0x400,2048,1);}
        else entry(p+5,"CHILD",0x500,3,0);
        unsigned stop=5+count*26,i=0;
        for(;i+4<=stop;i+=4)c=rot(c)^g32(p+i);
        for(;i<stop;++i)c=rot(c)^p[i];
        for(i=2008;i<2044;i+=4)c=rot(c)^g32(p+i);
    }else{
        p[0]=7;memcpy(p+4,"SBPr",4);p32(p+8,4);memcpy(p+28,"ROOT",4);p32(p+12,2048);
        p32(p+16,count);p32(p+20,root?16:8);p32(p+24,parent);
        unsigned names=32+28*count;
        if(root){bigentry(p,32,"PROOF/BIN",0x300,16*B+17,0,0);bigentry(p,60,"DIR",0x400,2048,1,12);memcpy(p+names,"PROOF/BIN",9);memcpy(p+names+12,"DIR",3);}
        else{bigentry(p,32,"CHILD",0x500,3,0,0);memcpy(p+names,"CHILD",5);}
        memcpy(p+2040,"oven",4);p[2044]=7;
        for(unsigned i=0;i<names+(root?16:8);i+=4)c=rot(c)^g32(p+i);
        c=rot(c)^g32(p+2040);c=rot(c)^p[2044];c=rot(c);c=rot(c);
    }
    p[2047]=fold(c);
}
static void fixture(void)
{
    memset(disk,0,sizeof disk);uint8_t*p=disk+4;
    p[0]=9;p[1]=16;p[2]=2;p[4]=12;p[5]=9;p[9]=1;p16(p+10,32);p32(p+12,0x202);
    p32(p+16,MAX);memcpy(p+22,"ACORN",5);p32(p+44,big?1:0);if(big)p32(p+48,2048);
    disk[3]=255;
    frag(512,16,2);frag(528,16,3);frag(544,16,4);frag(560,16,5);frag(576,16,3);
    frag(592,176,0);bits(disk,8,12,592-8);zonecheck();
    dir(1,0x202,1);dir(32,0x202,0);memcpy(blk(48),"yes",3);
    for(unsigned i=0;i<16*B;++i)blk(16)[i]=(uint8_t)(i*17+7);
    for(unsigned i=0;i<17;++i)blk(64)[i]=(uint8_t)(i*3+5);
    memcpy(saved,disk,sizeof disk);
}
static int rd(void*c,uint64_t off,void*out,uint32_t n)
{(void)c;if(fail||off>sizeof disk||n>sizeof disk-off||off%sector||n%sector)return -1;memcpy(out,disk+off,n);return 0;}
static void*alloc(void*c,size_t n){(void)c;void*p=malloc(n);if(p)++allocs;return p;}
static void drop(void*c,void*p){(void)c;if(p){assert(allocs);--allocs;free(p);}}
static int real_rd(void*c,uint64_t off,void*out,uint32_t n)
{
    FILE*f=c;if(off%sector||n%sector||off>INT64_MAX||fseek(f,(long)off,SEEK_SET))return -1;
    return fread(out,1,n,f)==n?0:-1;
}
static int real_walk(const IfsFsCoreOpsV2*core,void*v,const char*path,unsigned depth,unsigned*count)
{
    if(depth>32)return 1;
    char name[256],full[4096];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
    for(unsigned index=0;index<10000;++index){
        int rc=core->directory_entry(v,path,index,&e);
        if(rc==IFS_FS_END)return 0;
        if(rc){fprintf(stderr,"list %s index %u rc=%d\n",path,index,rc);return 1;}
        int n=snprintf(full,sizeof full,"%s%s%s",path,!strcmp(path,"/")?"":"/",name);
        if(n<0||(size_t)n>=sizeof full||++*count>10000)return 1;
        IfsFsEntryV2 check={.structure_bytes=sizeof check,.name=name,.name_capacity=sizeof name};
        rc=core->stat(v,full,&check);
        if(rc||check.type!=e.type||check.size_bytes!=e.size_bytes){fprintf(stderr,"stat %s rc=%d\n",full,rc);return 1;}
        printf("%s %llu bytes type %u\n",full,(unsigned long long)e.size_bytes,e.type);
        if(e.type==IFS_FS_ENTRY_DIRECTORY){if(real_walk(core,v,full,depth+1,count))return 1;}
        else if(e.type==IFS_FS_ENTRY_FILE){
            uint8_t buf[4096];for(uint64_t at=0;at<e.size_bytes;){
                uint32_t n=e.size_bytes-at>sizeof buf?sizeof buf:(uint32_t)(e.size_bytes-at);
                rc=core->read(v,full,at,buf,n);if(rc){fprintf(stderr,"read %s rc=%d\n",full,rc);return 1;}at+=n;
            }
        }
    }
    return 1;
}
static int real_test(const IfsFsCoreOpsV2*core,const char*path,unsigned want)
{
    FILE*f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);
    for(sector=512;sector<=4096;sector*=8){
        assert(size%sector==0);
        IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof h,.capabilities=IFS_FS_HOST_READ_ONLY,
          .volume_bytes=(uint64_t)size,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
          .context=f,.read_at=real_rd,.allocate=alloc,.release=drop};
        uint32_t variant=0;int rc=core->probe(&h,&variant);
        if(rc){fprintf(stderr,"real probe %s rc=%d sector=%u\n",path,rc,sector);return 1;}
        assert(variant==want&&!allocs);
        void*v=NULL;IfsFsVolumeInfoV2 i={.structure_bytes=sizeof i};assert(core->mount(&h,&v,&i)==0&&v);
        unsigned count=0;rc=real_walk(core,v,"/",0,&count);
        assert(core->unmount(v)==0&&!allocs);if(rc)return rc;
        printf("Real Acorn image %s sector=%u: %u entries, %llu free bytes\n",path,sector,count,(unsigned long long)i.free_bytes);
    }
    assert(!fclose(f));return 0;
}
int main(int argc,char**argv)
{
    if(argc==4&&!strcmp(argv[1],"--emit")){
        big=1;fixture();FILE*f=fopen(argv[2],"wb"),*p=fopen(argv[3],"wb");assert(f&&p);
        assert(fwrite(disk,1,sizeof disk,f)==sizeof disk);assert(fwrite(blk(16),1,16*B,p)==16*B);
        assert(fwrite(blk(64),1,17,p)==17);assert(!fclose(f)&&!fclose(p));return 0;
    }
    assert(argc==2||argc==4);void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
    IfsFsModuleApiEntryV2 entry_;*(void**)(&entry_)=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(entry_);
    const IfsFsModuleApiV2*api=entry_();assert(ifs_fs_api_v2_valid(api,"adfs"));
    const IfsFsCoreOpsV2*core=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2*vol=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);assert(core&&vol&&!core->write);
    if(argc==4){int rc=real_test(core,argv[2],(unsigned)strtoul(argv[3],NULL,10));dlclose(lib);return rc;}
    for(big=0;big<2;++big)for(sector=512;sector<=4096;sector*=8){
        fixture();IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof h,.capabilities=IFS_FS_HOST_READ_ONLY,
          .volume_bytes=sizeof disk,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
          .read_at=rd,.allocate=alloc,.release=drop};
        uint32_t variant=0;int rc=core->probe(&h,&variant);if(rc)fprintf(stderr,"probe rc=%d big=%u\n",rc,big);assert(!rc&&variant==big+1&&!allocs);
        void*v=NULL;IfsFsVolumeInfoV2 info={.structure_bytes=sizeof info};assert(core->mount(&h,&v,&info)==0&&v&&info.free_bytes==176*B);
        char name[256];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
        assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"PROOF.BIN"));
        assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,"DIR"));
        assert(core->directory_entry(v,"/",2,&e)==IFS_FS_END);
        assert(core->stat(v,"/proof.bin",&e)==0&&e.size_bytes==16*B+17);
        uint8_t out[16*B+17];assert(core->read(v,"/proof.bin",0,out,sizeof out)==0&&!memcmp(out,blk(16),16*B)&&!memcmp(out+16*B,blk(64),17));
        assert(core->read(v,"/dir/child",0,out,3)==0&&!memcmp(out,"yes",3));
        uint32_t n;assert(vol->label(v,name,sizeof name,&n)==0&&n==5&&!strcmp(name,"ACORN"));
        blk(1)[2047]^=1;assert(core->stat(v,"/proof.bin",&e)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        fail=1;assert(core->stat(v,"/proof.bin",&e)!=0);fail=0;
        assert(core->unmount(v)==0&&core->unmount(v)==IFS_FS_INVALID&&!allocs);
        disk[0]^=1;assert(core->probe(&h,&variant)==IFS_FS_CORRUPT&&!allocs);memcpy(disk,saved,sizeof disk);
        bits(disk,8,12,593-8);zonecheck();assert(core->probe(&h,&variant)==IFS_FS_CORRUPT&&!allocs);memcpy(disk,saved,sizeof disk);
        assert(!memcmp(disk,saved,sizeof disk));
    }
    puts("ADFS new map, free chain, E/F and F+ directories, fragmented files PASS");dlclose(lib);return 0;
}

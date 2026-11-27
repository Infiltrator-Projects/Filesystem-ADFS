/* SPDX-License-Identifier: GPL-3.0-or-later
 * Acorn ADFS new-map read-only engine. On-disk layout references:
 * Linux v6.12 ADFS disc record, map and E/F/F+ directory definitions;
 * original ADFS format work by Russell King (1997-2002).
 * The engine below is independently written for the Common V2 host.
 * No Linux VFS or external filesystem services. Old maps are not guessed.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
enum{MAX_NAME=255,MAX_ZONES=4096,MAX_DIR=4*1024*1024,SLOTS=8,DR=60,DRBITS=480};
typedef struct{uint64_t start,length;uint32_t id,zone;unsigned free;} Fragment;
typedef struct{uint32_t first,count;} Zone;
typedef struct{
    IfsFsHostRuntimeV2 host;uint64_t bytes,free_bytes,map_addr,unit;
    uint32_t block,zones,zonebits,idlen,ids,share,root,root_size,variant;
    Fragment *frags;Zone *zone;uint32_t count;
    char label[11];uintptr_t token;
} Volume;
typedef struct{uint32_t id,parent,bytes,type,attr;} Node;
typedef struct{Node node;uint32_t length;char name[MAX_NAME+1];} Entry;
static Volume slots[SLOTS];static uintptr_t generation;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static int leave(int r){atomic_flag_clear_explicit(&gate,memory_order_release);return r;}
static void copy(void*d,const void*s,size_t n){uint8_t*a=d;const uint8_t*b=s;while(n--)*a++=*b++;}
static void zero(void*d,size_t n){uint8_t*a=d;while(n--)*a++=0;}
static int same(const void*a,const void*b,size_t n){const uint8_t*p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static uint32_t le16(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8;}
static uint32_t le24(const uint8_t*p){return le16(p)|(uint32_t)p[2]<<16;}
static uint32_t le32(const uint8_t*p){return le16(p)|le16(p+2)<<16;}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int eq(const char*a,const char*b,uint32_t n){while(n--)if(lower((uint8_t)*a++)!=lower((uint8_t)*b++))return 0;return 1;}
static int cancel(const Volume*v){return v->host.cancelled&&v->host.cancelled(v->host.context);}
static int rd(const Volume*v,uint64_t off,void*p,uint32_t n)
{return ifs_fs_host_read_bytes_v2(&v->host,off,p,n);}
static void dispose(Volume*v)
{
    if(v->frags)v->host.release(v->host.context,v->frags);
    if(v->zone)v->host.release(v->host.context,v->zone);
    zero(v,sizeof(*v));
}
static uint32_t bits(const uint8_t*p,uint32_t at,uint32_t n)
{uint32_t x=0;for(uint32_t i=0;i<n;++i)x|=(uint32_t)((p[(at+i)/8]>>((at+i)%8))&1u)<<i;return x;}
/* FileCore zone checksum is a four-lane end-around carry sum, starting
 * from the last word and excluding the check byte itself. */
static uint8_t checkzone(const uint8_t*p,uint32_t n)
{
    uint32_t lane[4]={0};
    for(uint32_t i=n;i>4;){
        i-=4;
        for(unsigned j=0;j<4;++j){
            unsigned prev=(j+3)%4;
            lane[j]+=p[i+j]+(lane[prev]>>8);lane[prev]&=255;
        }
    }
    lane[0]+=lane[3]>>8;
    for(unsigned j=1;j<4;++j)lane[j]+=p[j]+(lane[j-1]>>8);
    return (uint8_t)(lane[0]^lane[1]^lane[2]^lane[3]);
}
static uint8_t checkboot(const uint8_t*p)
{
    uint32_t c=0;
    for(uint32_t i=511;i;){c=(c&255)+(c>>8);c+=p[--i];}
    return (uint8_t)c;
}
static int record(Volume*v,const uint8_t*p)
{
    uint32_t log=p[0],logunit=p[5],id=p[4],zones=p[9]|(uint32_t)p[42]<<8;
    if(log<8||log>10||id<log+3||id>19||logunit>30||!zones)return IFS_FS_NOT_FOUND;
    uint64_t bytes=(uint64_t)le32(p+36)<<32|le32(p+16);
    if(!bytes)return IFS_FS_NOT_FOUND;
    uint32_t version=le32(p+44);
    if(version>2||(!version&&id>16)||zones>MAX_ZONES||p[43]||p[40]&0xf0||p[41]&0xfe)
        return IFS_FS_UNSUPPORTED;
    for(unsigned i=52;i<60;++i)if(p[i])return IFS_FS_UNSUPPORTED;
    uint32_t b=1u<<log,spare=le16(p+10);
    uint64_t unit=UINT64_C(1)<<logunit;
    if(spare<32||spare>=b*8-DRBITS-id||bytes>v->host.volume_bytes||
       bytes%b||bytes%unit||bytes/b>UINT32_MAX)return IFS_FS_CORRUPT;
    uint32_t zonebits=b*8-spare;
    uint64_t totalbits=bytes/unit,capacity=(uint64_t)zones*zonebits-DRBITS;
    if(totalbits>capacity||totalbits<=(uint64_t)(zones-1)*zonebits-(zones>1?DRBITS:0))
        return IFS_FS_CORRUPT;
    uint64_t mapbit=(uint64_t)(zones/2)*zonebits;
    if(zones>1){if(mapbit<DRBITS)return IFS_FS_CORRUPT;mapbit-=DRBITS;}
    uint64_t map=mapbit*unit;
    if(map%b||map>bytes||(uint64_t)zones*b>bytes-map)return IFS_FS_CORRUPT;
    uint32_t root=le32(p+12),size=version?le32(p+48):2048;
    if((root>>8)<2||(root>>8)>=(1u<<id)||!size||size%2048||size>MAX_DIR)return IFS_FS_CORRUPT;
    v->bytes=bytes;v->unit=unit;v->block=b;v->zones=zones;v->zonebits=zonebits;
    v->idlen=id;v->ids=zonebits/(id+1);v->share=p[40]&15;v->map_addr=map;
    v->root=root;v->root_size=size;v->variant=version?2:1;
    for(unsigned i=0;i<10&&p[22+i];++i){
        if(p[22+i]<32||p[22+i]>=127)return IFS_FS_UNSUPPORTED;
        v->label[i]=(char)p[22+i];
    }
    return IFS_FS_OK;
}
/* Decode every map fragment once. Free fragments are identified only
 * through a validated forward chain, never by guessing from their ID.
 * The first zone's disc record consumes 480 bits of logical map capacity. */
static int load_map(Volume*v,const uint8_t*record0)
{
    uint64_t max=(uint64_t)v->zones*v->zonebits/(v->idlen+1)+v->zones;
    if(max>524288)return IFS_FS_UNSUPPORTED;
    v->frags=v->host.allocate(v->host.context,(size_t)max*sizeof(*v->frags));
    v->zone=v->host.allocate(v->host.context,(size_t)v->zones*sizeof(*v->zone));
    if(!v->frags||!v->zone)return IFS_FS_NO_MEMORY;
    uint8_t map[1024],cross=0;
    uint64_t logical=0,total=v->bytes/v->unit;
    for(uint32_t z=0;z<v->zones;++z){
        if(cancel(v))return IFS_FS_BUSY;
        int rc=rd(v,v->map_addr+(uint64_t)z*v->block,map,v->block);if(rc)return rc;
        if(checkzone(map,v->block)!=map[0])return IFS_FS_CORRUPT;
        cross^=map[3];
        if(!z){
            /* The boot record supplies geometry. Its disc identity fields
             * can be uninitialised; the checked map's record is authoritative. */
            const uint8_t *mr=map+4;
            if(!same(mr,record0,20)||!same(mr+36,record0+36,24))return IFS_FS_CORRUPT;
            zero(v->label,sizeof(v->label));
            for(unsigned i=0;i<10&&mr[22+i];++i){
                if(mr[22+i]<32||mr[22+i]>=127)return IFS_FS_UNSUPPORTED;
                v->label[i]=(char)mr[22+i];
            }
        }
        uint32_t start=z?32:32+DRBITS,end=32+v->zonebits;
        if(z+1==v->zones){
            if(total<logical||total-logical>end-start)return IFS_FS_CORRUPT;
            end=start+(uint32_t)(total-logical);
        }
        uint32_t link=bits(map,8,v->idlen>15?15:v->idlen);
        uint32_t freeat=link?8+link:0;
        v->zone[z].first=v->count;v->zone[z].count=0;
        for(uint32_t pos=start;pos<end;){
            if(end-pos<=v->idlen)return IFS_FS_CORRUPT;
            uint32_t id=bits(map,pos,v->idlen),last=pos+v->idlen;
            while(last<end&&!bits(map,last,1))++last;
            if(last==end||v->count>=max)return IFS_FS_CORRUPT;
            uint32_t len=last+1-pos;
            unsigned isfree=pos==freeat;
            if(freeat&&freeat<pos)return IFS_FS_CORRUPT;
            if(isfree){
                uint32_t next=id&0x7fff;
                if(next&&next<len)return IFS_FS_CORRUPT;
                freeat=next?pos+next:0;
                if(freeat>=end)return IFS_FS_CORRUPT;
                v->free_bytes+=(uint64_t)len*v->unit;
            }else if(!id)return IFS_FS_CORRUPT;
            if(logical>total||len>total-logical)return IFS_FS_CORRUPT;
            v->frags[v->count++]=(Fragment){logical*v->unit,(uint64_t)len*v->unit,id,z,isfree};
            ++v->zone[z].count;logical+=len;pos=last+1;
        }
        if(freeat)return IFS_FS_CORRUPT;
    }
    if(cross!=255||logical!=total)return IFS_FS_CORRUPT;
    /* The map itself must be in allocated root-fragment space. */
    uint64_t end=v->map_addr+(uint64_t)v->zones*v->block,covered=v->map_addr;
    for(uint32_t i=0;i<v->count&&covered<end;++i){
        const Fragment*x=&v->frags[i];
        if(x->start+x->length<=covered)continue;
        if(x->start>covered||x->free||x->id!=2)return IFS_FS_CORRUPT;
        covered=x->start+x->length;
    }
    return covered>=end?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int file_read(const Volume*v,const Node*n,uint64_t at,void*out,uint32_t len)
{
    if((!out&&len)||at>n->bytes||len>n->bytes-at)return IFS_FS_INVALID;
    uint32_t frag=n->id>>8,share=n->id&255;
    if(frag<2||frag>=(1u<<v->idlen))return IFS_FS_CORRUPT;
    uint64_t offset=at;
    if(share)offset+=(uint64_t)(share-1)*(UINT64_C(1)<<v->share)*v->block;
    uint32_t zone=frag==2?v->zones/2:frag/v->ids;
    if(zone>=v->zones)return IFS_FS_CORRUPT;
    uint8_t*dst=out;
    for(uint32_t i=0;i<v->zones&&len;++i){
        if(cancel(v))return IFS_FS_BUSY;
        const Zone*z=&v->zone[(zone+i)%v->zones];
        for(uint32_t j=0;j<z->count&&len;++j){
            const Fragment*x=&v->frags[z->first+j];
            if(x->free||x->id!=frag)continue;
            if(offset>=x->length){offset-=x->length;continue;}
            uint32_t chunk=x->length-offset<len?(uint32_t)(x->length-offset):len;
            uint64_t phys=x->start+offset;
            if(phys<v->map_addr+(uint64_t)v->zones*v->block&&v->map_addr<phys+chunk)
                return IFS_FS_CORRUPT;
            int rc=rd(v,phys,dst,chunk);if(rc)return rc;
            len-=chunk;dst+=chunk;offset=0;
        }
    }
    return len?IFS_FS_CORRUPT:IFS_FS_OK;
}
static uint32_t rot(uint32_t x){return (x>>13)|(x<<19);}
static uint8_t fold(uint32_t x){return (uint8_t)(x^(x>>8)^(x>>16)^(x>>24));}
static int decode_name(const uint8_t*p,uint32_t n,char*out)
{
    if(!n||n>MAX_NAME)return IFS_FS_CORRUPT;
    unsigned dots=0;
    for(uint32_t i=0;i<n;++i){
        if(p[i]<32||p[i]>=127||p[i]==92||p[i]=='.')return IFS_FS_UNSUPPORTED;
        if(p[i]=='/'){out[i]='.';++dots;}else out[i]=(char)p[i];
    }
    if(n<=2&&dots==n)out[0]='^';
    out[n]=0;return IFS_FS_OK;
}
static int root(const Volume*v,Node*n)
{*n=(Node){.id=v->root,.parent=v->root,.bytes=v->root_size,.type=IFS_FS_ENTRY_DIRECTORY,.attr=8};return IFS_FS_OK;}
static int child(const Volume*v,const Node*dir,const char*name,uint32_t length,uint32_t index,Entry*out)
{
    if(dir->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    uint32_t size=dir->bytes;
    if(!size||size>MAX_DIR||size%2048||(v->variant==1&&size!=2048))return IFS_FS_CORRUPT;
    uint8_t*p=v->host.allocate(v->host.context,size);if(!p)return IFS_FS_NO_MEMORY;
    int rc=file_read(v,dir,0,p,size);if(rc)goto done;
    uint32_t count=0,entries=0,names=0,nameslen=0,parent=0,c=0;
    if(v->variant==1){
        if((!same(p+1,"Nick",4)&&!same(p+1,"Hugo",4))||!same(p+1,p+2043,4)||
           p[0]!=p[2042]||p[2007]||p[2008]||p[2009]){rc=IFS_FS_CORRUPT;goto done;}
        entries=5;while(count<77&&p[5+count*26])++count;
        uint32_t stop=5+count*26,i=0;
        for(;i+4<=stop;i+=4)c=rot(c)^le32(p+i);
        for(;i<stop;++i)c=rot(c)^p[i];
        for(i=2008;i<2044;i+=4)c=rot(c)^le32(p+i);
        if(fold(c)!=p[2047]){rc=IFS_FS_CORRUPT;goto done;}
        parent=le24(p+2010);
    }else{
        uint32_t own=le32(p+8);count=le32(p+16);nameslen=le32(p+20);parent=le32(p+24);
        if(p[1]||p[2]||p[3]||!same(p+4,"SBPr",4)||le32(p+12)!=size||
           !same(p+size-8,"oven",4)||p[size-4]!=p[0]||p[size-3]||p[size-2]||
           own>size-36){rc=IFS_FS_CORRUPT;goto done;}
        entries=28+((own+3)&~3u);
        if(entries>size-8||count>(size-8-entries)/28){rc=IFS_FS_CORRUPT;goto done;}
        names=entries+count*28;
        if(nameslen>size-8-names||nameslen%4){rc=IFS_FS_CORRUPT;goto done;}
        for(uint32_t i=0;i<names+nameslen;i+=4)c=rot(c)^le32(p+i);
        c=rot(c)^le32(p+size-8);c=rot(c)^p[size-4];c=rot(c);c=rot(c);
        if(fold(c)!=p[size-1]){rc=IFS_FS_CORRUPT;goto done;}
    }
    if(parent!=dir->parent){rc=IFS_FS_CORRUPT;goto done;}
    unsigned found=0;Entry result;
    for(uint32_t i=0;i<count;++i){
        if(cancel(v)){rc=IFS_FS_BUSY;goto done;}
        Entry e={0};const uint8_t*q,*np;uint32_t n;
        if(v->variant==1){
            q=p+entries+i*26;np=q;n=0;while(n<10&&q[n]>=32)++n;
            e.node.bytes=le32(q+18);e.node.id=le24(q+22);e.node.attr=q[25];
        }else{
            q=p+entries+i*28;n=le32(q+20);uint32_t ptr=le32(q+24);
            if(ptr>nameslen||n>nameslen-ptr){rc=IFS_FS_CORRUPT;goto done;}
            np=p+names+ptr;e.node.bytes=le32(q+8);e.node.id=le32(q+12);e.node.attr=le32(q+16);
        }
        rc=decode_name(np,n,e.name);if(rc)goto done;
        e.length=n;e.node.type=e.node.attr&8?IFS_FS_ENTRY_DIRECTORY:IFS_FS_ENTRY_FILE;e.node.parent=dir->id;
        if(e.node.id>>8<2||(e.node.id>>8)>=(1u<<v->idlen)||
           (e.node.type==IFS_FS_ENTRY_DIRECTORY&&(e.node.id==dir->id||!e.node.bytes||e.node.bytes%2048))){
            rc=IFS_FS_CORRUPT;goto done;
        }
        if(name?(n==length&&eq(e.name,name,n)):i==index){
            if(found++){rc=IFS_FS_CORRUPT;goto done;}result=e;
        }
    }
    if(found){*out=result;rc=IFS_FS_OK;}else rc=name?IFS_FS_NOT_FOUND:IFS_FS_END;
done:v->host.release(v->host.context,p);return rc;
}
static int discover(const IfsFsHostRuntimeV2*h,Volume*out)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    Volume v={0};size_t n=h->structure_bytes;if(n>sizeof v.host)n=sizeof v.host;
    copy(&v.host,h,n);v.host.structure_bytes=(uint32_t)n;
    if(h->volume_bytes<2048)return IFS_FS_NOT_FOUND;
    uint8_t raw[512],dr[DR];int rc=IFS_FS_NOT_FOUND;
    if(h->volume_bytes>=3584){
        rc=rd(&v,3072,raw,512);if(rc)return rc;
        if(checkboot(raw)==raw[511]){
            copy(dr,raw+448,DR);rc=record(&v,dr);
            if(rc!=IFS_FS_NOT_FOUND&&rc)return rc;
        }else rc=IFS_FS_NOT_FOUND;
    }
    if(rc==IFS_FS_NOT_FOUND){
        rc=rd(&v,0,raw,512);if(rc)return rc;
        copy(dr,raw+4,DR);
        if(dr[9]!=1||dr[42])return IFS_FS_NOT_FOUND;
        rc=record(&v,dr);if(rc)return rc;
    }
    rc=load_map(&v,dr);
    if(!rc){Node rootnode;Entry e;root(&v,&rootnode);rc=child(&v,&rootnode,NULL,0,UINT32_MAX,&e);if(rc==IFS_FS_END)rc=IFS_FS_OK;}
    if(rc){dispose(&v);return rc;}
    *out=v;return IFS_FS_OK;
}
static Volume *mounted(void *p)
{if(p)for(unsigned i=0;i<SLOTS;++i)if(slots[i].token==(uintptr_t)p)return &slots[i];return NULL;}
static void info(const Volume *v,IfsFsVolumeInfoV2 *o)
{*o=(IfsFsVolumeInfoV2){.structure_bytes=sizeof(*o),.variant=v->variant,.max_name_bytes=v->variant==1?10:MAX_NAME,
.block_bytes=v->block,.total_bytes=v->bytes,.free_bytes=v->free_bytes};}
static int probe_impl(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{if(!variant)return IFS_FS_INVALID;Volume v={0};int rc=discover(h,&v);if(!rc)*variant=v.variant;dispose(&v);return rc;}
static int mount_impl(const IfsFsHostRuntimeV2 *h,void **out,IfsFsVolumeInfoV2 *o)
{
    if(!out||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    *out=NULL;if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    unsigned i=0;while(i<SLOTS&&slots[i].token)++i;
    if(i==SLOTS||generation==UINTPTR_MAX)return IFS_FS_BUSY;
    int rc=discover(h,&slots[i]);if(rc)return rc;
    slots[i].token=++generation;*out=(void *)slots[i].token;info(&slots[i],o);return IFS_FS_OK;
}
static int unmount_impl(void *p)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;dispose(v);return IFS_FS_OK;}
static int resolve(const Volume *v,const char *path,Entry *out)
{
    if(!path||*path!='/')return IFS_FS_INVALID;
    Entry e={.length=1,.name="/"};int rc=root(v,&e.node);if(rc)return rc;
    ++path;
    for(unsigned depth=0;*path;++depth){
        if(depth>=256)return IFS_FS_INVALID;
        uint32_t n=0;while(n<=MAX_NAME&&path[n]&&path[n]!='/')++n;
        if(!n||n>MAX_NAME||(n==1&&path[0]=='.')||(n==2&&same(path,"..",2)))return IFS_FS_INVALID;
        Node dir=e.node;rc=child(v,&dir,path,n,0,&e);if(rc)return rc;
        path+=n;if(*path=='/'){++path;if(!*path)return IFS_FS_INVALID;}
    }
    *out=e;return IFS_FS_OK;
}
static int emit(const Entry *e,IfsFsEntryV2 *o)
{
    if(!o||o->structure_bytes<sizeof(*o)||!o->name)return IFS_FS_INVALID;
    if(o->name_capacity<=e->length)return IFS_FS_NO_SPACE;
    char *name=o->name;uint32_t cap=o->name_capacity;
    *o=(IfsFsEntryV2){.structure_bytes=sizeof(*o),.type=e->node.type,.size_bytes=e->node.bytes,
      .name=name,.name_capacity=cap,.name_bytes=e->length};
    copy(name,e->name,e->length+1);return IFS_FS_OK;
}
static int stat_impl(void *p,const char *path,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);return rc?rc:emit(&e,o);}
static int list_impl(void *p,const char *path,uint32_t i,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry dir,e;int rc=resolve(v,path,&dir);if(!rc)rc=child(v,&dir.node,NULL,0,i,&e);return rc?rc:emit(&e,o);}
static int read_impl(void *p,const char *path,uint64_t at,void *out,uint32_t n)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);if(rc)return rc;return e.node.type==IFS_FS_ENTRY_FILE?file_read(v,&e.node,at,out,n):IFS_FS_UNSUPPORTED;}

static int query_impl(void*p,IfsFsVolumeInfoV2*o)
{Volume*v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;info(v,o);return IFS_FS_OK;}
static int label_impl(void*p,char*out,uint32_t cap,uint32_t*n)
{
    if(!n)return IFS_FS_INVALID;
    *n=0;Volume*v=mounted(p);if(!v||!out)return IFS_FS_INVALID;
    uint32_t len=0;while(v->label[len])++len;
    if(cap<=len)return IFS_FS_NO_SPACE;
    copy(out,v->label,len+1);*n=len;return IFS_FS_OK;
}
#define WRAP(name,args,call) static int name args{if(!enter())return IFS_FS_BUSY;return leave(call);}
WRAP(probe,(const IfsFsHostRuntimeV2*h,uint32_t*v),probe_impl(h,v))
WRAP(mount,(const IfsFsHostRuntimeV2*h,void**p,IfsFsVolumeInfoV2*o),mount_impl(h,p,o))
WRAP(unmount,(void*p),unmount_impl(p))
WRAP(stat_path,(void*p,const char*s,IfsFsEntryV2*o),stat_impl(p,s,o))
WRAP(list_path,(void*p,const char*s,uint32_t i,IfsFsEntryV2*o),list_impl(p,s,i,o))
WRAP(read_path,(void*p,const char*s,uint64_t a,void*b,uint32_t n),read_impl(p,s,a,b,n))
WRAP(query,(void*p,IfsFsVolumeInfoV2*o),query_impl(p,o))
WRAP(label_path,(void*p,char*s,uint32_t c,uint32_t*n),label_impl(p,s,c,n))
static const IfsFsCoreOpsV2 core={.header={2,sizeof(core),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},.probe=probe,.mount=mount,.unmount=unmount,.stat=stat_path,.directory_entry=list_path,.read=read_path};
static const IfsFsVolumeOpsV2 volume={.header={2,sizeof(volume),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_LABEL_GET},.query=query,.label=label_path};
static const IfsFsGroupHeaderV2*group(uint32_t id,uint32_t lo,uint32_t hi)
{if(lo>2||hi<2)return NULL;if(id==IFS_FS_GROUP_CORE)return &core.header;if(id==IFS_FS_GROUP_VOLUME)return &volume.header;return NULL;}
static const IfsFsModuleApiV2 api={.abi_version=2,.structure_bytes=sizeof(api),.format_id="adfs",
#ifdef IFS_BOOT_FIRMWARE
.runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
.runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
.group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),.query_group=group};
__attribute__((visibility("default")))const IfsFsModuleApiV2*infiltrator_filesystem_api_v2(void){return &api;}

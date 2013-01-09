#include <stdlib.h>
#include <string.h>
#include "intra_fit_tools.h"
#include "stats_tools.h"
#include "image.h"
#include "svd.h"

#define PRINT_PROGRESS (1)
#define PRINT_BLOCKS (0)
#define WRITE_IMAGES (1)

typedef struct pred_data pred_data;

struct pred_data{
  int    n;
  double mean[5*B_SZ*B_SZ];
  double cov[5*B_SZ*B_SZ][5*B_SZ*B_SZ];
};

static void pred_data_init(pred_data *_this){
  int i;
  int j;
  _this->n=0;
  for(i=0;i<5*B_SZ*B_SZ;i++){
    _this->mean[i]=0;
    for(j=0;j<5*B_SZ*B_SZ;j++){
      _this->cov[i][j]=0;
    }
  }
}

static void pred_data_add_block(pred_data *_this,const od_coeff *_block,
 int _stride){
  double delta[5*B_SZ*B_SZ];
  int    by;
  int    bx;
  int    j;
  int    i;
  for(by=0;by<=1;by++){
    for(bx=0;bx<=2;bx++){
      int             bo;
      const od_coeff *block;
      if(by==1&&bx==2){
        continue;
      }
      bo=B_SZ*B_SZ*(3*by+bx);
      block=&_block[_stride*B_SZ*(by-1)+B_SZ*(bx-1)];
      for(j=0;j<B_SZ;j++){
        for(i=0;i<B_SZ;i++){
          delta[bo+B_SZ*j+i]=block[_stride*j+i]-_this->mean[bo+B_SZ*j+i];
          _this->mean[bo+B_SZ*j+i]+=delta[bo+B_SZ*j+i]/_this->n;
        }
      }
    }
  }
  for(j=0;j<5*B_SZ*B_SZ;j++){
    for(i=0;i<5*B_SZ*B_SZ;i++){
      _this->cov[i][j]+=delta[i]*delta[j]*(_this->n-1)/_this->n;
    }
  }
}

#define PRINT_COMP (0)

static void pred_data_update(pred_data *_this,int _mode){
  int     i;
  int     j;
  int     k;
  double  scale[5*B_SZ*B_SZ];
  double  xtx[2*4*B_SZ*B_SZ][4*B_SZ*B_SZ];
  double  xty[4*B_SZ*B_SZ][B_SZ*B_SZ];
  double *xtxp[2*4*B_SZ*B_SZ];
  double  s[4*B_SZ*B_SZ];
  double  beta_0[B_SZ*B_SZ];
  double  beta_1[4*B_SZ*B_SZ][B_SZ*B_SZ];

  /* compute the scale factors */
  for(i=0;i<5*B_SZ*B_SZ;i++){
    scale[i]=sqrt(_this->cov[i][i]);
  }

  /* normalize X^T*X and X^T*Y */
  for(j=0;j<4*B_SZ*B_SZ;j++){
    for(i=0;i<4*B_SZ*B_SZ;i++){
      xtx[j][i]=_this->cov[j][i]/(scale[j]*scale[i]);
    }
    for(i=0;i<B_SZ*B_SZ;i++){
      xty[j][i]=_this->cov[j][4*B_SZ*B_SZ+i]/(scale[j]*scale[4*B_SZ*B_SZ+i]);
    }
  }

#if PRINT_COMP
  fprintf(stderr,"xtx=[");
  for(j=0;j<4*B_SZ*B_SZ;j++){
    fprintf(stderr,"%s",j!=0?";":"");
    for(i=0;i<4*B_SZ*B_SZ;i++){
      fprintf(stderr,"%s%- 24.18G",i!=0?",":"",xtx[j][i]);
    }
  }
  fprintf(stderr,"];\n");

  fprintf(stderr,"xty=[");
  for(j=0;j<4*B_SZ*B_SZ;j++){
    fprintf(stderr,"%s",j!=0?";":"");
    for(i=0;i<B_SZ*B_SZ;i++){
      fprintf(stderr,"%s%- 24.18G",i!=0?",":"",xty[j][i]);
    }
  }
  fprintf(stderr,"];\n");
#endif

  /* compute the pseudo-inverse of X^T*X */
  for(i=0;i<2*4*B_SZ*B_SZ;i++){
    xtxp[i]=xtx[i];
  }
  svd_pseudoinverse(xtxp,s,4*B_SZ*B_SZ,4*B_SZ*B_SZ);

#if PRINT_COMP
  fprintf(stderr,"xtxi=[");
  for(j=0;j<4*B_SZ*B_SZ;j++){
    fprintf(stderr,"%s",j!=0?";":"");
    for(i=0;i<4*B_SZ*B_SZ;i++){
      fprintf(stderr,"%s%- 24.18G",i!=0?",":"",xtx[j][i]);
    }
  }
  fprintf(stderr,"];\n");
#endif

  /* compute beta_1 = (X^T*X)^-1 * X^T*Y and beta_0 = Ym - Xm * beta_1 */
  for(i=0;i<B_SZ*B_SZ;i++){
    beta_0[i]=_this->mean[4*B_SZ*B_SZ+i];
    for(j=0;j<4*B_SZ*B_SZ;j++){
      beta_1[j][i]=0;
      for(k=0;k<4*B_SZ*B_SZ;k++){
        beta_1[j][i]+=xtx[j][k]*xty[k][i];
      }
      beta_1[j][i]*=scale[4*B_SZ*B_SZ+i]/scale[j];
      beta_0[i]-=_this->mean[j]*beta_1[j][i];
    }
  }

#if PRINT_COMP
  fprintf(stderr,"beta_1=[");
  for(j=0;j<4*B_SZ*B_SZ;j++){
    fprintf(stderr,"%s",j!=0?";":"");
    for(i=0;i<B_SZ*B_SZ;i++){
      fprintf(stderr,"%s%- 24.18G",i!=0?",":"",beta_1[j][i]);
    }
  }
  fprintf(stderr,"];\n");

  fprintf(stderr,"beta_0=[");
  for(i=0;i<B_SZ*B_SZ;i++){
    fprintf(stderr,"%s%- 24.18G",i!=0?",":"",beta_0[i]);
  }
  fprintf(stderr,"];\n");
#endif

  /* update the predictor constants */
  for(i=0;i<B_SZ*B_SZ;i++){
    int y;
    int x;
    y=i/B_SZ;
    x=i%B_SZ;
#if B_SZ==4
    NE_PRED_OFFSETS_4x4[_mode][y][x]=beta_0[i];
#else
# error "Need a predictors for this block size."
#endif
    for(j=0;j<B_SZ*B_SZ;j++){
      int v;
      int u;
      v=j/B_SZ;
      u=j%B_SZ;
      for(k=0;k<4;k++){
        NE_PRED_WEIGHTS_4x4[_mode][y][x][k][v][u]=beta_1[k*B_SZ*B_SZ+j][i];
      }
      NE_PRED_WEIGHTS_4x4[_mode][y][x][4][v][u]=0;
    }
  }
}

typedef struct intra_pred_ctx intra_pred_ctx;

struct intra_pred_ctx{
  int         step;
  intra_stats stats;
  intra_stats gb;
  pred_data   pd[OD_INTRA_NMODES];
  image_data  img;
#if WRITE_IMAGES
  image_files files;
#endif
};

static void intra_pred_ctx_init(intra_pred_ctx *_this){
  int i;
  intra_stats_init(&_this->stats);
  intra_stats_init(&_this->gb);
  for(i=0;i<OD_INTRA_NMODES;i++){
    pred_data_init(&_this->pd[i]);
  }
}

static void intra_pred_ctx_update(intra_pred_ctx *_this){
  int i;
  for(i=0;i<OD_INTRA_NMODES;i++){
    pred_data_update(&_this->pd[i],i);
  }
}

static int init_start(void *_ctx,const char *_name,const th_info *_ti,int _pli,
 int _nxblocks,int _nyblocks){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  printf("in init_start\n");
#endif
  fprintf(stdout,"%s\n",_name);
  ctx=(intra_pred_ctx *)_ctx;
  image_data_init(&ctx->img,_name,_nxblocks,_nyblocks);
  return EXIT_SUCCESS;
}

static int init_finish(void *_ctx){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  printf("in init_finish\n");
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_save_map(&ctx->img);
  image_data_clear(&ctx->img);
  return EXIT_SUCCESS;
}

static void ip_pre_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_pre_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_pre_block(&ctx->img,_data,_stride,_bi,_bj);
}

static void ip_fdct_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_fdct_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_fdct_block(&ctx->img,_bi,_bj);
}

/* select the initial mode based on the VP8 mode classification */
static void ip_init_mode(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_init_mode\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  ctx->img.mode[_bj*ctx->img.nxblocks+_bi]=vp8_select_mode(_data,_stride,NULL);
}

static void ip_print_blocks(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
  image_data     *img;
  od_coeff       *block;
  int             mode;
  int             by;
  int             bx;
  int             j;
  int             i;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_print_blocks\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  img=&ctx->img;
  mode=img->mode[_bj*img->nxblocks+_bi];
  fprintf(stderr,"%i",mode);
  for(by=0;by<=1;by++){
    for(bx=0;bx<=2;bx++){
      if(by==1&&bx==2){
        continue;
      }
      block=&img->fdct[img->fdct_stride*B_SZ*(_bj+by)+B_SZ*(_bi+bx)];
      for(j=0;j<B_SZ;j++){
        for(i=0;i<B_SZ;i++){
          fprintf(stderr," %i",block[img->fdct_stride*j+i]);
        }
      }
    }
  }
  fprintf(stderr,"\n");
}

static void ip_pred_data(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
  image_data     *img;
  od_coeff       *block;
  int             mode;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_pred_data\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  img=&ctx->img;

  mode=img->mode[_bj*img->nxblocks+_bi];
  block=&img->fdct[img->fdct_stride*B_SZ*(_bj+1)+B_SZ*(_bi+1)];

  ctx->pd[mode].n++;
  pred_data_add_block(&ctx->pd[mode],block,img->fdct_stride);
}

static int pred_start(void *_ctx,const char *_name,const th_info *_ti,int _pli,
 int _nxblocks,int _nyblocks){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  printf("in pred_start\n");
#endif
  fprintf(stdout,"%s\n",_name);
  ctx=(intra_pred_ctx *)_ctx;
  image_data_init(&ctx->img,_name,_nxblocks,_nyblocks);
#if WRITE_IMAGES
  image_files_init(&ctx->files,_nxblocks,_nyblocks);
#endif
  image_data_load_map(&ctx->img);
  return EXIT_SUCCESS;
}

static int pred_finish(void *_ctx){
  intra_pred_ctx *ctx;
#if WRITE_IMAGES
  char step[16];
#endif
#if PRINT_PROGRESS
  printf("in pred_finish\n");
#endif
  ctx=(intra_pred_ctx *)_ctx;
  intra_stats_combine(&ctx->gb,&ctx->stats);
  intra_stats_correct(&ctx->stats);
  intra_stats_print(&ctx->stats,"Daala Intra Predictors",OD_SCALE);
  image_data_save_map(&ctx->img);
  image_data_clear(&ctx->img);
#if WRITE_IMAGES
  sprintf(step,"-step%02i",ctx->step);
  image_files_write(&ctx->files,ctx->img.name,step);
  image_files_clear(&ctx->files);
#endif
  return EXIT_SUCCESS;
}

static void ip_pred_mode(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_pred_mode\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_mode_block(&ctx->img,_bi,_bj);
}

static void ip_pred_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_pred_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_pred_block(&ctx->img,_bi,_bj);
}

static void ip_idct_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_idct_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_idct_block(&ctx->img,_bi,_bj);
}

static void ip_post_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_post_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_post_block(&ctx->img,_bi,_bj);
}

static void ip_stats_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_stats_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_stats_block(&ctx->img,_data,_stride,_bi,_bj,&ctx->stats);
}

#if WRITE_IMAGES
static void ip_files_block(void *_ctx,const unsigned char *_data,int _stride,
 int _bi,int _bj){
  intra_pred_ctx *ctx;
#if PRINT_PROGRESS
  if(_bi==0&&_bj==0){
    printf("in ip_files_block\n");
  }
#endif
  ctx=(intra_pred_ctx *)_ctx;
  image_data_files_block(&ctx->img,_data,_stride,_bi,_bj,&ctx->files);
}
#endif

const block_func INIT[]={
  ip_pre_block,
  ip_fdct_block,
  ip_init_mode,
#if PRINT_BLOCKS
  ip_print_blocks,
#endif
  ip_pred_data
};

const int NINIT=sizeof(INIT)/sizeof(*INIT);

const block_func KMEANS[]={
  ip_pre_block,
  ip_fdct_block,
  ip_pred_mode,
  ip_pred_data,
  ip_pred_block,
  ip_stats_block,
  ip_idct_block,
  ip_post_block,
#if WRITE_IMAGES
  ip_files_block
#endif
};

const int NKMEANS=sizeof(KMEANS)/sizeof(*KMEANS);

#define PADDING (4*B_SZ)
#if PADDING<3*B_SZ
# error "PADDING must be at least 3*B_SZ"
#endif

int main(int _argc,const char *_argv[]){
  intra_pred_ctx ctx;
  int            ret;
  int            s;
  /* initialize some constants */
  vp8_scale_init(VP8_SCALE);
  od_scale_init(OD_SCALE);
  intra_map_colors(COLORS,OD_INTRA_NMODES);
  /* first pass across images uses VP8 SATD mode selection */
  intra_pred_ctx_init(&ctx);
  ret=apply_to_blocks2(&ctx,PADDING,init_start,INIT,NINIT,init_finish,0x1,
   _argc,_argv);
  /* each k-means step uses Daala SATD mode selection */
  for(s=1;s<=1;s++){
    printf("Step %02i\n",s);
    ctx.step=s;
    /* update the intra predictors model */
    intra_pred_ctx_update(&ctx);
    intra_pred_ctx_init(&ctx);
    /*print_betas();*/
    ret=apply_to_blocks2(&ctx,PADDING,pred_start,KMEANS,NKMEANS,pred_finish,0x1,
     _argc,_argv);
    intra_stats_correct(&ctx.gb);
    intra_stats_print(&ctx.gb,"Daala Intra Predictors",OD_SCALE);
  }
  return ret;
}
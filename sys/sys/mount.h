/*	$OpenBSD: mount.h,v 1.144 2019/04/02 13:07:28 visa Exp $	*/
/*	$NetBSD: mount.h,v 1.48 1996/02/18 11:55:47 fvdl Exp $	*/

/*
 * Copyright (c) 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mount.h	8.15 (Berkeley) 7/14/94
 */

#ifndef _SYS_MOUNT_H_
#define _SYS_MOUNT_H_

#include <sys/cdefs.h>
#ifndef _KERNEL
#include <sys/ucred.h>
#endif
#include <sys/queue.h>
#include <sys/rwlock.h>

typedef struct { int32_t val[2]; } fsid_t;	/* file system id type */

/*
 * File identifier.
 * These are unique per filesystem on a single machine.
 */
#define	MAXFIDSZ	16

struct fid {
	u_short		fid_len;		/* length of data in bytes */
	u_short		fid_reserved;		/* force longword alignment */
	char		fid_data[MAXFIDSZ];	/* data (variable length) */
};

/*
 * Export arguments for local filesystem mount calls.
 */
struct export_args {
	int	ex_flags;		/* export related flags */
	uid_t	ex_root;		/* mapping for root uid */
	struct	xucred ex_anon;		/* mapping for anonymous user */
	struct	sockaddr *ex_addr;	/* net address to which exported */
	int	ex_addrlen;		/* and the net address length */
	struct	sockaddr *ex_mask;	/* mask of valid bits in saddr */
	int	ex_masklen;		/* and the smask length */
};

/*
 * Arguments to mount UFS-based filesystems
 */
struct ufs_args {
	char	*fspec;			/* block special device to mount */
	struct	export_args export_info;/* network export information */
};

/*
 * Arguments to mount MFS
 */
struct mfs_args {
	char	*fspec;			/* name to export for statfs */
	struct	export_args export_info;/* if exported MFSes are supported */
	caddr_t	base;			/* base of file system in memory */
	u_long	size;			/* size of file system */
};

/*
 * Arguments to mount ISO 9660 filesystems.
 */
struct iso_args {
	char	*fspec;			/* block special device to mount */
	struct	export_args export_info;/* network export info */
	int	flags;			/* mounting flags, see below */
	int	sess;			/* start sector of session */
};

#define	ISOFSMNT_NORRIP		0x00000001	/* disable Rock Ridge Ext.*/
#define	ISOFSMNT_GENS		0x00000002	/* enable generation numbers */
#define	ISOFSMNT_EXTATT		0x00000004	/* enable extended attr. */
#define	ISOFSMNT_NOJOLIET	0x00000008	/* disable Joliet Ext.*/
#define	ISOFSMNT_SESS		0x00000010	/* use iso_args.sess */

/*
 * Arguments to mount tmpfs file systems
 */
#define TMPFS_ARGS_VERSION	1
struct tmpfs_args {
	int			ta_version;

	/* Size counters. */
	ino_t			ta_nodes_max;
	off_t			ta_size_max;

	/* Root node attributes. */
	uid_t			ta_root_uid;
	gid_t			ta_root_gid;
	mode_t			ta_root_mode;
};

/*
 * file system statistics
 */

#define	MFSNAMELEN	16	/* length of fs type name, including nul */
#define	MNAMELEN	90	/* length of buffer for returned name */

/* per-filesystem mount options */
union mount_info {
	struct ufs_args ufs_args;
	struct mfs_args mfs_args;
	struct iso_args iso_args;
	struct tmpfs_args tmpfs_args;
	char __align[160];	/* 64-bit alignment and room to grow */
};

/* new statfs structure with mount options and statvfs fields */
struct statfs {
	u_int32_t	f_flags;	/* copy of mount flags */
	u_int32_t	f_bsize;	/* file system block size */
	u_int32_t	f_iosize;	/* optimal transfer block size */

					/* unit is f_bsize */
	u_int64_t  	f_blocks;	/* total data blocks in file system */
	u_int64_t  	f_bfree;	/* free blocks in fs */
	int64_t  	f_bavail;	/* free blocks avail to non-superuser */

	u_int64_t 	f_files;	/* total file nodes in file system */
	u_int64_t  	f_ffree;	/* free file nodes in fs */
	int64_t  	f_favail;	/* free file nodes avail to non-root */

	u_int64_t  	f_syncwrites;	/* count of sync writes since mount */
	u_int64_t  	f_syncreads;	/* count of sync reads since mount */
	u_int64_t  	f_asyncwrites;	/* count of async writes since mount */
	u_int64_t  	f_asyncreads;	/* count of async reads since mount */

	fsid_t	   	f_fsid;		/* file system id */
	u_int32_t	f_namemax;      /* maximum filename length */
	uid_t	   	f_owner;	/* user that mounted the file system */
	u_int64_t  	f_ctime;	/* last mount [-u] time */

	char f_fstypename[MFSNAMELEN];	/* fs type name */
	char f_mntonname[MNAMELEN];	/* directory on which mounted */
	char f_mntfromname[MNAMELEN];	/* mounted file system */
	char f_mntfromspec[MNAMELEN];	/* special for mount request */
	union mount_info mount_info;	/* per-filesystem mount options */
};


/*
 * File system types.
 */
#define	MOUNT_FFS	"ffs"		/* UNIX "Fast" Filesystem */
#define	MOUNT_UFS	MOUNT_FFS	/* for compatibility */
#define	MOUNT_MFS	"mfs"		/* Memory Filesystem */
#define	MOUNT_AFS	"afs"		/* Andrew Filesystem */
#define	MOUNT_CD9660	"cd9660"	/* ISO9660 (aka CDROM) Filesystem */
#define	MOUNT_EXT2FS	"ext2fs"	/* Second Extended Filesystem */
#define	MOUNT_NCPFS	"ncpfs"		/* NetWare Network File System */
#define	MOUNT_TMPFS	"tmpfs"		/* tmpfs */

/*
 * Structure per mounted file system.  Each mounted file system has an
 * array of operations and an instance record.  The file systems are
 * put on a doubly linked list.
 */
LIST_HEAD(vnodelst, vnode);

struct mount {
	TAILQ_ENTRY(mount) mnt_list;		/* mount list */
	SLIST_ENTRY(mount) mnt_dounmount;	/* unmount work queue */
	const struct vfsops *mnt_op;		/* operations on fs */
	struct vfsconf  *mnt_vfc;               /* configuration info */
	struct vnode	*mnt_vnodecovered;	/* vnode we mounted on */
	struct vnode    *mnt_syncer;            /* syncer vnode */
	struct vnodelst	mnt_vnodelist;		/* list of vnodes this mount */
	struct rwlock   mnt_lock;               /* mount structure lock */
	int		mnt_flag;		/* flags */
	struct statfs	mnt_stat;		/* cache of filesystem stats */
	void		*mnt_data;		/* private data */
};

/*
 * Mount flags.
 *
 * Unmount uses MNT_FORCE flag.
 */
#define	MNT_RDONLY	0x00000001	/* read only filesystem */
#define	MNT_SYNCHRONOUS	0x00000002	/* file system written synchronously */
#define	MNT_NOEXEC	0x00000004	/* can't exec from filesystem */
#define	MNT_NOSUID	0x00000008	/* don't honor setuid bits on fs */
#define	MNT_NODEV	0x00000010	/* don't interpret special files */
#define	MNT_NOPERM	0x00000020	/* don't enforce permission checks */
#define	MNT_ASYNC	0x00000040	/* file system written asynchronously */
#define	MNT_WXALLOWED	0x00000800	/* filesystem allows W|X mappings */

/*
 * exported mount flags.
 */
#define	MNT_EXRDONLY	0x00000080	/* exported read only */
#define	MNT_EXPORTED	0x00000100	/* file system is exported */
#define	MNT_DEFEXPORTED	0x00000200	/* exported to the world */
#define	MNT_EXPORTANON	0x00000400	/* use anon uid mapping for everyone */

/*
 * Flags set by internal operations.
 */
#define	MNT_LOCAL	0x00001000	/* filesystem is stored locally */
#define	MNT_QUOTA	0x00002000	/* quotas are enabled on filesystem */
#define	MNT_ROOTFS	0x00004000	/* identifies the root filesystem */

/*
 * Extra post 4.4BSD-lite2 mount flags.
 */
#define MNT_NOATIME	0x00008000	/* don't update access times on fs */

/*
 * Mask of flags that are visible to statfs()
 */
#define	MNT_VISFLAGMASK	0x0400ffff

#define	MNT_BITS \
    "\20\001RDONLY\002SYNCHRONOUS\003NOEXEC\004NOSUID\005NODEV\006NOPERM" \
    "\007ASYNC\010EXRDONLY\011EXPORTED\012DEFEXPORTED\013EXPORTANON" \
    "\014WXALLOWED\015LOCAL\016QUOTA\017ROOTFS\020NOATIME"

/*
 * filesystem control flags.
 */
#define	MNT_UPDATE	0x00010000	/* not a real mount, just an update */
#define	MNT_DELEXPORT	0x00020000	/* delete export host lists */
#define	MNT_RELOAD	0x00040000	/* reload filesystem data */
#define	MNT_FORCE	0x00080000	/* force unmount or readonly change */
#define	MNT_STALLED	0x00100000	/* filesystem stalled */ 
#define	MNT_SWAPPABLE	0x00200000	/* filesystem can be used for swap */
#define MNT_WANTRDWR	0x02000000	/* want upgrade to read/write */
#define MNT_SOFTDEP     0x04000000      /* soft dependencies being done */
#define MNT_DOOMED	0x08000000	/* device behind filesystem is gone */

/*
 * Flags for various system call interfaces.
 *
 * waitfor flags to vfs_sync() and getfsstat()
 */
#define MNT_WAIT	1	/* synchronously wait for I/O to complete */
#define MNT_NOWAIT	2	/* start all I/O, but do not wait for it */
#define MNT_LAZY	3	/* push data not written by filesystem syncer */

/*
 * Generic file handle
 */
struct fhandle {
	fsid_t	fh_fsid;	/* File system id of mount point */
	struct	fid fh_fid;	/* File sys specific id */
};
typedef struct fhandle	fhandle_t;

/*
 * Sysctl CTL_VFS definitions.
 *
 * Second level identifier specifies which filesystem. Second level
 * identifier VFS_GENERIC returns information about all filesystems.
 */
#define	VFS_GENERIC	0	/* generic filesystem information */
/*
 * Third level identifiers for VFS_GENERIC are given below; third
 * level identifiers for specific filesystems are given in their
 * mount specific header files.
 */
#define VFS_MAXTYPENUM	1	/* int: highest defined filesystem type */
#define VFS_CONF	2	/* struct: vfsconf for filesystem given
				   as next argument */
#define VFS_BCACHESTAT	3	/* struct: buffer cache statistics given 
				   as next argument */
#define	CTL_VFSGENCTL_NAMES { \
	{ 0, 0 }, \
	{ "maxtypenum", CTLTYPE_INT }, \
	{ "conf", CTLTYPE_NODE }, \
	{ "bcachestat", CTLTYPE_STRUCT } \
}

/*
 * Filesystem configuration information. One of these exists for each
 * type of filesystem supported by the kernel. These are searched at
 * mount time to identify the requested filesystem.
 */
struct vfsconf {
	const struct vfsops *vfc_vfsops; /* filesystem operations vector */
	char	vfc_name[MFSNAMELEN];	/* filesystem type name */
	int	vfc_typenum;		/* historic filesystem type number */
	u_int	vfc_refcount;		/* number mounted of this type */
	int	vfc_flags;		/* permanent flags */
	size_t	vfc_datasize;		/* size of data args */
};

/* buffer cache statistics */
struct bcachestats {
	int64_t numbufs;		/* number of buffers allocated */
	int64_t numbufpages;		/* number of pages in buffer cache */
	int64_t numdirtypages; 		/* number of dirty free pages */
	int64_t numcleanpages; 		/* number of clean free pages */
	int64_t pendingwrites;		/* number of pending writes */
	int64_t pendingreads;		/* number of pending reads */
	int64_t numwrites;		/* total writes started */
	int64_t numreads;		/* total reads started */
	int64_t cachehits;		/* total reads found in cache */
	int64_t busymapped;		/* number of busy and mapped buffers */
	int64_t dmapages;		/* dma reachable pages in buffer cache */
	int64_t highpages;		/* pages above dma region */
	int64_t delwribufs;		/* delayed write buffers */
	int64_t kvaslots;		/* kva slots total */
	int64_t kvaslots_avail;		/* available kva slots */
	int64_t highflips;		/* total flips to above DMA */
	int64_t highflops;		/* total failed flips to above DMA */
	int64_t dmaflips;		/* total flips from high to DMA */
};
#ifdef _KERNEL
extern struct bcachestats bcstats;
extern long buflowpages, bufhighpages, bufbackpages;
#define BUFPAGES_DEFICIT (((buflowpages - bcstats.numbufpages) < 0) ? 0 \
    : buflowpages - bcstats.numbufpages)
#define BUFPAGES_INACT (((bcstats.numcleanpages - buflowpages) < 0) ? 0 \
    : bcstats.numcleanpages - buflowpages)
extern int bufcachepercent;
extern void bufadjust(int);
struct uvm_constraint_range;
extern int bufbackoff(struct uvm_constraint_range*, long);

/*
 * Operations supported on mounted file system.
 */
struct nameidata;
struct mbuf;

extern int maxvfsconf;		/* highest defined filesystem type */

struct vfsops {
	int	(*vfs_mount)(struct mount *mp, const char *path,
				    void *data,
				    struct nameidata *ndp, struct proc *p);
	int	(*vfs_start)(struct mount *mp, int flags,
				    struct proc *p);
	int	(*vfs_unmount)(struct mount *mp, int mntflags,
				    struct proc *p);
	int	(*vfs_root)(struct mount *mp, struct vnode **vpp);
	int	(*vfs_quotactl)(struct mount *mp, int cmds, uid_t uid,
				    caddr_t arg, struct proc *p);
	int	(*vfs_statfs)(struct mount *mp, struct statfs *sbp,
				    struct proc *p);
	int	(*vfs_sync)(struct mount *mp, int waitfor, int stall,
				    struct ucred *cred, struct proc *p);
	int	(*vfs_vget)(struct mount *mp, ino_t ino,
				    struct vnode **vpp);
	int	(*vfs_fhtovp)(struct mount *mp, struct fid *fhp,
				     struct vnode **vpp);
	int	(*vfs_vptofh)(struct vnode *vp, struct fid *fhp);
	int	(*vfs_init)(struct vfsconf *);
	int     (*vfs_sysctl)(int *, u_int, void *, size_t *, void *,
				     size_t, struct proc *);
	int	(*vfs_checkexp)(struct mount *mp, struct mbuf *nam,
				    int *extflagsp, struct ucred **credanonp);
};

#define VFS_MOUNT(MP, PATH, DATA, NDP, P) \
	(*(MP)->mnt_op->vfs_mount)(MP, PATH, DATA, NDP, P)
#define VFS_START(MP, FLAGS, P)	  (*(MP)->mnt_op->vfs_start)(MP, FLAGS, P)
#define VFS_UNMOUNT(MP, FORCE, P) (*(MP)->mnt_op->vfs_unmount)(MP, FORCE, P)
#define VFS_ROOT(MP, VPP)	  (*(MP)->mnt_op->vfs_root)(MP, VPP)
#define VFS_QUOTACTL(MP,C,U,A,P)  (*(MP)->mnt_op->vfs_quotactl)(MP, C, U, A, P)
#define VFS_STATFS(MP, SBP, P)	  (*(MP)->mnt_op->vfs_statfs)(MP, SBP, P)
#define VFS_SYNC(MP, W, S, C, P)  (*(MP)->mnt_op->vfs_sync)(MP, W, S, C, P)
#define VFS_VGET(MP, INO, VPP)	  (*(MP)->mnt_op->vfs_vget)(MP, INO, VPP)
#define VFS_FHTOVP(MP, FIDP, VPP) \
	(*(MP)->mnt_op->vfs_fhtovp)(MP, FIDP, VPP)
#define	VFS_VPTOFH(VP, FIDP)	  (*(VP)->v_mount->mnt_op->vfs_vptofh)(VP, FIDP)
#define VFS_CHECKEXP(MP, NAM, EXFLG, CRED) \
	(*(MP)->mnt_op->vfs_checkexp)(MP, NAM, EXFLG, CRED)


#include <net/radix.h>
#include <sys/socket.h>		/* XXX for AF_MAX */

/*
 * Network address lookup element
 */
struct netcred {
	struct	radix_node netc_rnodes[2];
	int	netc_exflags;
	int	netc_len;			/* size of the allocation */
	struct	ucred netc_anon;
};

/*
 * Network export information
 */
struct netexport {
	struct	netcred ne_defexported;		/* Default export */
	struct	radix_node_head *ne_rtable_inet;/* Individual exports */
};

/*
 * exported vnode operations
 */
int	vfs_busy(struct mount *, int);
#define VB_READ		0x01
#define VB_WRITE	0x02
#define VB_NOWAIT	0x04	/* immediately fail on busy lock */
#define VB_WAIT		0x08	/* sleep fail on busy lock */
#define VB_DUPOK	0x10	/* permit duplicate mount busying */

int     vfs_isbusy(struct mount *);
struct	mount *vfs_mount_alloc(struct vnode *, struct vfsconf *);
void	vfs_mount_free(struct mount *);
int     vfs_mount_foreach_vnode(struct mount *, int (*func)(struct vnode *,
				    void *), void *);
void	vfs_getnewfsid(struct mount *);
struct	mount *vfs_getvfs(fsid_t *);
int	vfs_mountedon(struct vnode *);
int	vfs_rootmountalloc(char *, char *, struct mount **);
void	vfs_unbusy(struct mount *);
extern	TAILQ_HEAD(mntlist, mount) mountlist;
int	vfs_stall(struct proc *, int);
void	vfs_stall_barrier(void);

struct	mount *getvfs(fsid_t *);	    /* return vfs given fsid */
					    /* process mount export info */
int	vfs_export(struct mount *, struct netexport *, struct export_args *);
					    /* lookup host in fs export list */
struct	netcred *vfs_export_lookup(struct mount *, struct netexport *,
	    struct mbuf *);
int	vfs_allocate_syncvnode(struct mount *);
int	speedup_syncer(void);

int	vfs_syncwait(struct proc *, int);   /* sync and wait for complete */
void	vfs_shutdown(struct proc *);	    /* unmount and sync file systems */
int	dounmount(struct mount *, int, struct proc *);
void	vfsinit(void);
struct	vfsconf *vfs_byname(const char *);
struct	vfsconf *vfs_bytypenum(int);
#else /* _KERNEL */
__BEGIN_DECLS
int	fstatfs(int, struct statfs *);
int	getfh(const char *, fhandle_t *);
int	getfsstat(struct statfs *, size_t, int);
int	getmntinfo(struct statfs **, int);
int	mount(const char *, const char *, int, void *);
int	statfs(const char *, struct statfs *);
int	unmount(const char *, int);
#if __BSD_VISIBLE
struct stat;
int	fhopen(const fhandle_t *, int);
int	fhstat(const fhandle_t *, struct stat *);
int	fhstatfs(const fhandle_t *, struct statfs *);
#endif /* __BSD_VISIBLE */
__END_DECLS
#endif /* _KERNEL */
#endif /* !_SYS_MOUNT_H_ */

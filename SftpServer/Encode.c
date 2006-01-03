/*
MySecureShell permit to add restriction to modified sftp-server
when using MySecureShell as shell.
Copyright (C) 2004 Sebastien Tardif

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation (version 2)

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "../config.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#ifdef HAVE_LINUX_EXT2_FS_H

#define EXT2_SECRM_FL                   0x00000001 /* Secure deletion */
#define EXT2_UNRM_FL                    0x00000002 /* Undelete */
#define EXT2_COMPR_FL                   0x00000004 /* Compress file */
#define EXT2_SYNC_FL                    0x00000008 /* Synchronous updates */
#define EXT2_IMMUTABLE_FL               0x00000010 /* Immutable file */
#define EXT2_APPEND_FL                  0x00000020 /* writes to file may only append */
#define EXT2_NODUMP_FL                  0x00000040 /* do not dump file */
#define EXT2_NOATIME_FL                 0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define EXT2_DIRTY_FL                   0x00000100
#define EXT2_COMPRBLK_FL                0x00000200 /* One or more compressed clusters */
#define EXT2_NOCOMP_FL                  0x00000400 /* Don't compress */
#define EXT2_ECOMPR_FL                  0x00000800 /* Compression error */
/* End compression flags --- maybe not all used */
#define EXT2_BTREE_FL                   0x00001000 /* btree format dir */
#define EXT2_INDEX_FL                   0x00001000 /* hash-indexed directory */
#define EXT2_IMAGIC_FL                  0x00002000 /* AFS directory */
#define EXT2_JOURNAL_DATA_FL            0x00004000 /* Reserved for ext3 */
#define EXT2_NOTAIL_FL                  0x00008000 /* file tail should not be merged */
#define EXT2_DIRSYNC_FL                 0x00010000 /* dirsync behaviour (directories only) */
#define EXT2_TOPDIR_FL                  0x00020000 /* Top of directory hierarchies*/
#define EXT2_RESERVED_FL                0x80000000 /* reserved for ext2 lib */

#define EXT2_FL_USER_VISIBLE            0x0003DFFF /* User visible flags */
#define EXT2_FL_USER_MODIFIABLE         0x000380FF /* User modifiable flags */

/*
 * ioctl commands
 */
#define EXT2_IOC_GETFLAGS               _IOR('f', 1, long)
#define EXT2_IOC_SETFLAGS               _IOW('f', 2, long)
#define EXT2_IOC_GETVERSION             _IOR('v', 1, long)
#define EXT2_IOC_SETVERSION             _IOW('v', 2, long)

#endif
#include <unistd.h>
#include "Encode.h"
#include "GetUsersInfos.h"
#include "Log.h"

tAttributes		*GetAttributes(tBuffer *bIn)
{
  static tAttributes	a;

  memset(&a, 0, sizeof(a));
  a.flags = BufferGetInt32(bIn);
  DEBUG((MYLOG_DEBUG, "FLAGS[%x][%i]", a.flags, a.flags));
  if (cVersion >= 4)
    a.type = BufferGetInt8(bIn);
  if (a.flags & SSH2_FILEXFER_ATTR_SIZE)
    a.size = BufferGetInt64(bIn);
  if (cVersion <= 3 && (a.flags & SSH2_FILEXFER_ATTR_UIDGID))
    {
      a.uid = BufferGetInt32(bIn);
      a.gid = BufferGetInt32(bIn);
    }
  if (cVersion >= 4 && (a.flags & SSH4_FILEXFER_ATTR_OWNERGROUP))
    {
      struct passwd	*pw;
      struct group	*gr;
      
      if ((pw = mygetpwnam(BufferGetString(bIn))))
	a.uid = pw->pw_uid;
      if ((gr = mygetgrnam(BufferGetString(bIn))))
	a.gid = gr->gr_gid;
    }
  if (a.flags & SSH2_FILEXFER_ATTR_PERMISSIONS)
    a.perm = BufferGetInt32(bIn);
  if (cVersion <= 3)
    {
      if (a.flags & SSH2_FILEXFER_ATTR_ACMODTIME)
	{
	  a.atime = BufferGetInt32(bIn);
	  a.mtime = BufferGetInt32(bIn);
	}
    }
  else //version >= 4
    {
      if (a.flags & SSH4_FILEXFER_ATTR_ACCESSTIME)
	a.atime = BufferGetInt64(bIn);
      if (a.flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferGetInt32(bIn);
      if (a.flags & SSH4_FILEXFER_ATTR_CREATETIME)
	a.ctime = BufferGetInt64(bIn);
      if (a.flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferGetInt32(bIn);
      if (a.flags & SSH4_FILEXFER_ATTR_MODIFYTIME)
	a.mtime = BufferGetInt64(bIn);
      if (a.flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferGetInt32(bIn);
    }
  if (a.flags & SSH2_FILEXFER_ATTR_ACL) //unsupported feature
    {
      free(BufferGetString(bIn));
    }
  if (a.flags & SSH2_FILEXFER_ATTR_EXTENDED) //unsupported feature
    {
      int	i, count;

      count = BufferGetInt32(bIn);
      for (i = 0; i < count; i++)
	{
	  free(BufferGetString(bIn));
	  free(BufferGetString(bIn));
	}
    }
  return (&a);
}

void	StatToAttributes(struct stat *st, tAttributes *a, char *fileName)
{
  memset(a, 0, sizeof(*a));
  a->flags = SSH2_FILEXFER_ATTR_SIZE;
  a->size = st->st_size;
  a->uid = st->st_uid;
  a->gid = st->st_gid;
  a->flags |= SSH2_FILEXFER_ATTR_PERMISSIONS;
  a->perm = st->st_mode;
  a->flags |= SSH2_FILEXFER_ATTR_ACMODTIME;
  a->atime = st->st_atime;
  a->mtime = st->st_mtime;
  a->ctime = st->st_ctime;
  if (cVersion >= 4)
    {
      if ((st->st_mode & S_IFMT) == S_IFREG)
	a->type = SSH4_FILEXFER_TYPE_REGULAR;
      else if ((st->st_mode & S_IFMT) == S_IFDIR)
	a->type = SSH4_FILEXFER_TYPE_DIRECTORY;
      else if ((st->st_mode & S_IFMT) == S_IFLNK)
	a->type = SSH4_FILEXFER_TYPE_SYMLINK;
      else
	a->type = SSH4_FILEXFER_TYPE_SPECIAL;
      if (cVersion >= 5)
	{
	  if ((st->st_mode & S_IFMT) == S_IFSOCK)
	    a->type = SSH5_FILEXFER_TYPE_SOCKET;
	  else if ((st->st_mode & S_IFMT) == S_IFCHR)
	    a->type = SSH5_FILEXFER_TYPE_CHAR_DEVICE;
	  else if ((st->st_mode & S_IFMT) == S_IFBLK)
	    a->type = SSH5_FILEXFER_TYPE_BLOCK_DEVICE;
	  else if ((st->st_mode & S_IFMT) == S_IFIFO)
	    a->type = SSH5_FILEXFER_TYPE_FIFO;
	}
      a->flags |= SSH4_FILEXFER_ATTR_OWNERGROUP | SSH4_FILEXFER_ATTR_ACCESSTIME
	| SSH4_FILEXFER_ATTR_CREATETIME | SSH4_FILEXFER_ATTR_MODIFYTIME;
    }
  else
    a->flags |= SSH2_FILEXFER_ATTR_UIDGID;
  if (cVersion >= 5 && fileName)
    {
      int	pos = strlen(fileName) - 1;
      int	fd;

      a->attrib = 0;
      a->flags |= SSH5_FILEXFER_ATTR_BITS;
      while (pos >= 1 && fileName[pos - 1] != '/')
	pos--;
      if (pos >= 0 && fileName[pos] == '.')
      a->attrib |= SSH5_FILEXFER_ATTR_FLAGS_HIDDEN;
#ifdef HAVE_LINUX_EXT2_FS_H
      if ((fd = open(fileName, O_RDONLY)) >= 0)
	{
	  int	flags;

	  if (ioctl(fd, EXT2_IOC_GETFLAGS, flags) != -1)
	    {
	      if (flags & EXT2_COMPR_FL)
		a->attrib |= SSH5_FILEXFER_ATTR_FLAGS_COMPRESSED;
	      if (flags & EXT2_APPEND_FL)
		a->attrib |= SSH5_FILEXFER_ATTR_FLAGS_APPEND_ONLY;
	      if (flags & EXT2_IMMUTABLE_FL)
		a->attrib |= SSH5_FILEXFER_ATTR_FLAGS_IMMUTABLE;
	      if (flags & EXT2_SYNC_FL)
		a->attrib |= SSH5_FILEXFER_ATTR_FLAGS_SYNC;
	    }
	  close(fd);
	}
#endif
    }
}

void	EncodeAttributes(tBuffer *b, tAttributes *a)
{
  BufferPutInt32(b, a->flags);
  if (cVersion >= 4)
    BufferPutInt8(b, a->type);
  if (a->flags & SSH2_FILEXFER_ATTR_SIZE)
    BufferPutInt64(b, a->size);
  if (cVersion <= 3 && a->flags & SSH2_FILEXFER_ATTR_UIDGID)
    {
      BufferPutInt32(b, a->uid);
      BufferPutInt32(b, a->gid);
    }
  if (a->flags & SSH4_FILEXFER_ATTR_OWNERGROUP)
    {
      struct passwd	*pw;
      struct group	*gr;
      char		buf[11+1];
      char		*str;
	
      if ((pw = mygetpwuid(a->uid)))
	str = pw->pw_name;
      else
	{
	  snprintf(buf, sizeof(buf), "%u", a->uid);
	  str = buf;
	}
      BufferPutString(b, str);
      if ((gr = mygetgrgid(a->gid)))
	str = gr->gr_name;
      else
	{
	  snprintf(buf, sizeof(buf), "%u", a->gid);
	  str = buf;
	}
      BufferPutString(b, str);
    }
  if (a->flags & SSH2_FILEXFER_ATTR_PERMISSIONS)
    BufferPutInt32(b, a->perm);
  if (cVersion <= 3)
    {
      if (a->flags & SSH2_FILEXFER_ATTR_ACMODTIME)
	{
	  BufferPutInt32(b, a->atime);
	  BufferPutInt32(b, a->mtime);
	}
    }
  else //cVersion >= 4
    {
      if (a->flags & SSH4_FILEXFER_ATTR_ACCESSTIME)
	BufferPutInt64(b, a->atime);
      if (a->flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferPutInt32(b, 0);
      if (a->flags & SSH4_FILEXFER_ATTR_CREATETIME)
	BufferPutInt64(b, a->ctime);
      if (a->flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferPutInt32(b, 0);	
      if (a->flags & SSH4_FILEXFER_ATTR_MODIFYTIME)
	BufferPutInt64(b, a->mtime);
      if (a->flags & SSH4_FILEXFER_ATTR_SUBSECOND_TIMES)
	BufferPutInt32(b, 0);
    }
  if (a->flags & SSH5_FILEXFER_ATTR_BITS)
    BufferPutInt32(b, a->attrib);
  if (a->flags & SSH2_FILEXFER_ATTR_ACL)
    BufferPutString(b, ""); //unsupported feature
  if (a->flags & SSH2_FILEXFER_ATTR_EXTENDED)
    BufferPutInt32(b, 0); //unsupported feature
}

struct timeval		*AttributesToTimeval(const tAttributes *a)
{
  static struct timeval	tv[2];

  tv[0].tv_sec = a->atime;
  tv[0].tv_usec = 0;
  tv[1].tv_sec = a->mtime;
  tv[1].tv_usec = 0;
  return (tv);
}

#include <NinePea.h>

/* paths */

enum {
  Qroot = 0,
  Qctl,
  Qdata,
  QNUM,
};

/* fid mapping functions */

struct hentry {
  unsigned long id;
  unsigned long data;
  struct hentry *next;
  struct hentry *prev;
};

struct htable {
  unsigned char length;
  struct hentry **data;
};

struct htable *fs_fids;

unsigned long
hashf(struct htable *tbl, unsigned long id) {
  return id % tbl->length;
}

struct hentry*
fs_fid_find(unsigned long id) {
  struct hentry **cur;

  for (cur = &(fs_fids->data[hashf(fs_fids, id)]);
        *cur != NULL; cur = &(*cur)->next) {
      if ((*cur)->id == id)
        break;
  }

  return *cur;
}

struct hentry*
fs_fid_add(unsigned long id, unsigned long data) {
  struct hentry *cur = fs_fid_find(id);
  unsigned char h;

  if (cur == NULL) {
    cur = (struct hentry*)calloc(1, sizeof(*cur));
    cur->id = id;
    cur->data = data;
    h = hashf(fs_fids, id);
    
    if (fs_fids->data[h]) {
      cur->next = fs_fids->data[h];
      cur->next->prev = cur;
    }
    fs_fids->data[h] = cur;

    return NULL;
  }

  cur->data = data;

  return cur;
}

void
fs_fid_del(unsigned long id) {
  unsigned char h = hashf(fs_fids, id);
  struct hentry *cur = fs_fids->data[h];
  
  if (cur->id == id)
    fs_fids->data[h] = cur->next;
  else {
    cur = cur->next;
    while (cur) {
      if (cur->id == id)
        break;

      cur = cur->next;
    }
  }

  if (cur == NULL) {
    return;
  }

  if (cur->prev)
    cur->prev->next = cur->next;
  if (cur->next)
    cur->next->prev = cur->prev;

  free(cur);
}

/* 9p handlers */

Fcall*
fs_version(Fcall *ifcall) {
  if (ifcall->msize > MAX_MSG)
    ifcall->msize = MAX_MSG;

  return ifcall;
}

Fcall*
fs_attach(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));

  ofcall->qid.type = QTDIR | QTTMP;

  fs_fid_add(ifcall->fid, Qroot);

  return ofcall;
}

Fcall*
fs_walk(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));
  unsigned long path;
  struct hentry *ent = fs_fid_find(ifcall->fid);
  int i;

  if (!ent) {
    ofcall->type = RError;
    ofcall->ename = strdup("file not found");

    return ofcall;
  }

  for (i = 0; i < ifcall->nwname; i++) {
    switch(ent->data) {
    case Qroot:
      if (!strcmp(ifcall->wname[i], "arductl")) {
        path = Qctl;
        ofcall->wqid[i].type = QTTMP;
        ofcall->wqid[i].path = Qctl;
        ofcall->wqid[i].version = 0;
      } 
      else if (!strcmp(ifcall->wname[i], "ardudata")) {
        path = Qdata;
        ofcall->wqid[i].type = QTTMP;
        ofcall->wqid[i].path = Qdata;
        ofcall->wqid[i].version = 0;
      } 
      else if (!strcmp(ifcall->wname[i], ".")) {
        path = Qroot;
        ofcall->wqid[i].type = QTTMP | QTDIR;
        ofcall->wqid[i].path = Qroot;
        ofcall->wqid[i].version = 0;
      } 
      else {
        ofcall->type = RError;
        ofcall->ename = strdup("file not found");

        return ofcall;
      }
      break;
    case Qdata:
    case Qctl:
      if (!strcmp(ifcall->wname[i], "..")) {
        path = Qroot;
        ofcall->wqid[i].type = QTTMP | QTDIR;
        ofcall->wqid[i].path = Qroot;
        ofcall->wqid[i].version = 0;
      } 
      else {
        ofcall->type = RError;
        ofcall->ename = strdup("file not found");

        return ofcall;
      }
      break;
    }
  }

  ofcall->nwqid = i;

  fs_fid_add(ifcall->newfid, path);

  return ofcall;
}

Fcall*
fs_stat(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));
  struct hentry *ent;

  if ((ent = fs_fid_find(ifcall->fid)) == NULL) {
    ofcall->type = RError;
    ofcall->ename = strdup("file not found");

    return ofcall;
  }

  ofcall->stat.qid.type = QTTMP;
  ofcall->stat.mode = 0666 | DMTMP;
  ofcall->stat.atime = ofcall->stat.mtime = ofcall->stat.length = 0;
  ofcall->stat.uid = strdup("none");
  ofcall->stat.gid = strdup("none");
  ofcall->stat.muid = strdup("none");

  switch (ent->data) {
  case Qroot:
    ofcall->stat.qid.type |= QTDIR;
    ofcall->stat.mode |= 0111 | DMDIR;
    ofcall->stat.name = strdup("arduino/");
    break;
  case Qctl:
    ofcall->stat.name = strdup("arduino/arductl");
    break;
  case Qdata:
    ofcall->stat.name = strdup("arduino/ardudata");
    break;
  }

  return ofcall;
}

Fcall*
fs_clunk(Fcall *ifcall) {
  fs_fid_del(ifcall->fid);

  return ifcall;
}

Fcall*
fs_open(Fcall *ifcall) {
  Fcall *ofcall;
  struct hentry *cur = fs_fid_find(ifcall->fid);

  if (!cur) {
    ofcall->type = RError;
    ofcall->ename = strdup("file not found");

    return ofcall;
  }

  ofcall = (Fcall*)calloc(1, sizeof(Fcall));
  ofcall->iounit = MAX_IO; // or so
  ofcall->qid.type = QTTMP;

  if (cur->data == Qroot)
    ofcall->qid.type |= QTDIR;

  return ofcall;
}

Fcall*
fs_read(Fcall *ifcall, unsigned char *out, unsigned int outlen) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));
  struct hentry *cur = fs_fid_find(ifcall->fid);
  Stat stat;
  char tmpstr[32];
  unsigned char i;
  unsigned long value;

  if (cur == NULL) {
    ofcall->type = RError;
    ofcall->ename = strdup("file not found");
  }
  else if (((unsigned long)cur->data) == Qdata) {
    snprintf((char*)out, outlen - 1, "digital pins:\n");

    for (i = 2; i < 14; i++) {
      if (digitalRead(i))
        snprintf(tmpstr, sizeof(tmpstr), "\t%d:\tHIGH\n", i);
      else
        snprintf(tmpstr, sizeof(tmpstr), "\t%d:\t LOW\n", i);

      strlcat((char*)out, tmpstr, outlen);
    }

    snprintf(tmpstr, sizeof(tmpstr), "analog pins:\n");
    strlcat((char*)out, tmpstr, outlen);

    for (i = A0; i <= A5; i++) {
      value = analogRead(i - A0);
      snprintf(tmpstr, sizeof(tmpstr), "\t%d:\t%04d\n", i, value);
      strlcat((char*)out, tmpstr, outlen);
    }

    ofcall->count = strlen((char*)out) - ifcall->offset;
    if (ofcall->count > ifcall->count)
      ofcall->count = ifcall->count;
    if (ifcall->offset != 0)
      memmove(out, &out[ifcall->offset], ofcall->count);
  } 
  // offset?  too hard, sorry
  else if (ifcall->offset != 0) {
    out[0] = '\0';
    ofcall->count = 0;
  }
  else if (((unsigned long)cur->data) == Qroot) {
    stat.type = 0;
    stat.dev = 0;
    stat.qid.type = QTTMP;
    stat.qid.version = 0;
    stat.qid.path = Qdata;
    stat.mode = 0666 | DMTMP;
    stat.atime = 0;
    stat.mtime = 0;
    stat.length = 0;
    stat.name = strdup("ardudata");
    stat.uid = strdup("none");
    stat.gid = strdup("none");
    stat.muid = strdup("none");
    ofcall->count = putstat(out, 0, &stat);

    stat.qid.path = Qctl;
    stat.name = strdup("arductl");
    stat.uid = strdup("none");
    stat.gid = strdup("none");
    stat.muid = strdup("none");
    
    ofcall->count += putstat(out, ofcall->count, &stat);
  }
  else if (((unsigned long)cur->data) == Qctl) {
    ofcall->type = RError;
    ofcall->ename = strdup("ctl file read does nothing...");
  }
  else {
    snprintf(tmpstr, sizeof(tmpstr), "unknown path: %x\n", (unsigned int)cur->data);
    ofcall->type = RError;
    ofcall->ename = strdup(tmpstr);
  }

  return ofcall;
}

Fcall*
fs_create(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));

  ofcall->type = RError;
  ofcall->ename = strdup("create not allowed");

  return ofcall;
}

Fcall*
fs_write(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));

  ofcall->type = RError;
  ofcall->ename = strdup("write not allowed");

  return ofcall;
}

Fcall*
fs_remove(Fcall *ifcall) {
  Fcall *ofcall = (Fcall*)calloc(1, sizeof(Fcall));

  ofcall->type = RError;
  ofcall->ename = strdup("remove not allowed");

  return ofcall;
}

Fcall*
fs_flush(Fcall *ifcall) {
  return ifcall;
}

Callbacks callbacks;

/* arduino stuff */

void die(unsigned char code) {
  pinMode(13, OUTPUT);	

  while(true) {
    digitalWrite(13, HIGH);
    delay(1000);
    digitalWrite(13, LOW);
    delay(code * 1000);
  }
}

void setup() {
  Serial.begin(115200);

  fs_fids = (htable*)malloc(sizeof(htable));
  fs_fids->length = 16;
  fs_fids->data = (hentry**)calloc(fs_fids->length, sizeof(hentry*));

  // this is REQUIRED by proc9p (see below)
  callbacks.version = fs_version;
  callbacks.attach = fs_attach;
  callbacks.flush = fs_flush;
  callbacks.walk = fs_walk;
  callbacks.open = fs_open;
  callbacks.create = fs_create;
  callbacks.read = fs_read;
  callbacks.write = fs_write;
  callbacks.clunk = fs_clunk;
  callbacks.remove = fs_remove;
  callbacks.stat = fs_stat;
}

unsigned int len = 0;
unsigned char msg[MAX_MSG+1];
unsigned int msglen = 0;
unsigned char out[MAX_MSG+1];

void loop() {
  unsigned int i;

  while (Serial.available()) {
    msg[len++] = Serial.read();

    if (len == msglen)
      break;

    if (len >= MAX_MSG)
      die(1);
  }

  if (len < msglen || len < 4)
    return;

  if (msglen == 0) {
    i = 0;
    get4(msg, i, msglen);

    if (msglen > MAX_MSG)
      die(3);

    return;
  }

  if (len == msglen) {
    // proc9p accepts valid 9P msgs of length msglen,
    // processes them using callbacks->various(functions);
    // returns variable out's msglen
    msglen = proc9p(msg, msglen, &callbacks, out);
    
    for (i = 0; i < msglen; i++)
      Serial.write(out[i]);
     
    len = msglen = 0;
     
    return;
  }

  die(5);
}
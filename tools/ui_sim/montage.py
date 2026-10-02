import sys, numpy as np
from PIL import Image, ImageDraw
def load(fn):
    a=np.fromfile(fn,dtype=np.uint16).reshape(412,412)
    r=((a>>11)&31)*255//31; g=((a>>5)&63)*255//63; b=(a&31)*255//31
    im=Image.fromarray(np.dstack([r,g,b]).astype(np.uint8))
    m=Image.new('L',(412,412),0); ImageDraw.Draw(m).ellipse((0,0,411,411),fill=255)
    out=Image.new('RGB',(412,412),(34,34,34)); out.paste(im,(0,0),m); return out
out=sys.argv[1]; files=sys.argv[2:]; cols=4
rows=(len(files)+cols-1)//cols
W=Image.new('RGB',(cols*425+10,rows*425+10),(34,34,34))
for i,f in enumerate(files): W.paste(load(f),(10+(i%cols)*425,10+(i//cols)*425))
W.save(out)

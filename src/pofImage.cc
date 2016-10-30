/*
 * Copyright (c) 2014 Antoine Rousseau <antoine@metalu.net>
 * BSD Simplified License, see the file "LICENSE.txt" in this distribution.
 * See https://github.com/Ant1r/ofxPof for documentation and updates.
 */
#include "pofImage.h"
#include "pofFbo.h"

class pofIm;

class pofImLoader: public ofThread {
	public :
    deque<pofIm*> imagesToLoad;

	void threadedFunction();
    
    void enQueue(pofIm *im) {
    	lock();
    	imagesToLoad.push_back(im);
    	unlock();
    }
    
    unsigned int getLen() {
    	int l;
    	lock();
    	l = imagesToLoad.size();
    	unlock();
    	return l;
    }
    	
} *imLoader = NULL, *imLoaderHTTP = NULL; // two loader tasks : one for online resources, one for on disk ones.
    

class pofIm{

	t_symbol *file;
	int refCount;
	
	static std::map<t_symbol*,pofIm*> images;
	static ofMutex mutex;

	public:
	ofImage im;
	bool loaded, preloaded, needUpdate;
	
	pofIm(t_symbol *f):file(f),refCount(1), loaded(false), preloaded(false), needUpdate(false) {
		images[file] = this;
		im.setAnchorPercent(0.5,0.5);
		im.setUseTexture(false);
	}
	
	~pofIm() {
		images.erase(file);
	}
	
	void doLoad(void) {
		if(loaded) return;
		if( (!strncmp(file->s_name, "http", strlen("http"))) || ofFile(file->s_name).exists())
            im.load(file->s_name);
		loaded = true;
	}
	
	void load() {
		if(!preloaded) {
			preloaded = true;
			mutex.lock();
			refCount++;
			if(!strncmp(file->s_name, "http", strlen("http"))) imLoaderHTTP->enQueue(this);
			else imLoader->enQueue(this);
			mutex.unlock();
		}
	}
	
	void draw(float x, float y, float w, float h) {
		if(!loaded) return;
		
		//mutex.lock(); // seems working without this...
		if(im.isAllocated()) {
			if(!im.isUsingTexture())
			{
                im.setUseTexture(true);
                needUpdate = true;
            }
            if(needUpdate) {
            	im.update();
            	needUpdate = false;
            }
			im.draw(x, y, w, h);
		}
		//mutex.unlock();
	}
		
	void bind() {
		if(!loaded) return;
		if(im.isAllocated()) {
			if(!im.isUsingTexture())
			{
                im.setUseTexture(true);
                needUpdate = true;
            }
            if(needUpdate) {
            	im.update();
            	needUpdate = false;
            }
			im.bind();
			pofBase::currentTexture = &im.getTexture();
		}
	}
	
	void unbind() {
		if(!loaded) return;
		/*if(im.isAllocated()) {
			if(!im.isUsingTexture())
			{
                im.setUseTexture(true);
                im.update();
            }
			im.unbind();
		}*/
		im.unbind();
	}
		
	static pofIm* getImage(t_symbol *file){
		mutex.lock();
		std::map<t_symbol*,pofIm*>::iterator it;
		it = images.find(file);
		if(it!=images.end()) {
			it->second->refCount++;
			mutex.unlock();
			return it->second;
		}
		else {
			new pofIm(file);
			mutex.unlock();
			return images[file];
		}
	}
		
	static pofIm* findImage(t_symbol *file){
		std::map<t_symbol*,pofIm*>::iterator it;
		it = images.find(file);
		if(it == images.end()) return NULL;
		else return it->second;
	}	
		
	static int letImage(pofIm *image) {
		bool deleted = false;
		mutex.lock();
		image->refCount--;
		if(image->refCount == 0) {
			delete image;
			deleted = true;
		}
		mutex.unlock();
		if(deleted) return 0;
		else return image->refCount;
	}
	
	static int getNumImages()
	{
		return images.size();
	}
};

std::map<t_symbol*,pofIm*> pofIm::images;
ofMutex pofIm::mutex;

//------------------------------------------//

void pofImLoader::threadedFunction() {
	while(isThreadRunning()) {
		if(imagesToLoad.size()) {
			lock();
			pofIm *im = imagesToLoad.front();
			imagesToLoad.pop_front();
			unlock();
			im->doLoad();
			pofIm::letImage(im);
		} else ofSleepMillis(5);
    }
}


//------------------------------------------//

t_class *pofimage_class;
t_symbol *s_set, *s_saved, *s_size, *s_monitor, *s_color;

void pofimage_set(void *x, t_symbol *f);

void *pofimage_new(t_symbol *sym,int argc, t_atom *argv)
{
    t_symbol *file=NULL;
    t_float w=0,h=0,xa=0,ya=0,sx=0,sy=0,sw=0,sh=0,istexture=0;

	if(argc>0) file = atom_getsymbol(&argv[0]);

    if((argc>1)&&(argv[1].a_type == A_SYMBOL)) istexture = 1;
    else {
 		if(argc>1) w = atom_getfloat(&argv[1]);
		if(argc>2) h = atom_getfloat(&argv[2]);

		if(argc>3) xa = atom_getfloat(&argv[3]);
		if(argc>4) ya = atom_getfloat(&argv[4]);

		if(argc>5) sx = atom_getfloat(&argv[5]);
		if(argc>6) sy = atom_getfloat(&argv[6]);

		if(argc>7) sw = atom_getfloat(&argv[7]);
		if(argc>8) sh = atom_getfloat(&argv[8]);
	}
	
    pofImage* obj = new pofImage(pofimage_class, file, w, h, xa, ya, sx, sy, sw, sh, istexture);
    
    floatinlet_new(&obj->pdobj->x_obj, &obj->width);
    floatinlet_new(&obj->pdobj->x_obj, &obj->height);

    obj->pdcanvas = canvas_getcurrent();
	if(file) pofimage_set(&obj->pdobj->x_obj, file);
	
    return (void*) (obj->pdobj);
}

void pofimage_free(void *x)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);

	if(px->image) pofIm::letImage(px->image);
	px->unreserveAll();
	delete px;
}

void pofimage_set(void *x, t_symbol *f)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);	
	px->set(f);
}

void pofimage_save(void *x, t_symbol *f)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);	
	px->save(f);
}

void pofimage_reserve(void *x, t_symbol *f)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	px->reserve(f);
}

void pofimage_unreserve(void *x, t_symbol *f)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);

	if(!strcmp(f->s_name, "_all_")) px->unreserveAll();
	else px->unreserve(f);
}

void pofimage_monitor(void *x, t_float f)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	px->monitor = (f != 0);
}

static void pofimage_out(void *x, t_symbol *s, int argc, t_atom *argv)
{
    pofImage* px = (pofImage*)(((PdObject*)x)->parent);
    
    outlet_anything(px->m_out2, s, argc, argv);
}

void pofimage_setcolor(void *x, t_symbol *s, int argc, t_atom *argv)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	if(px->image == NULL) return;

	t_float X=0.0, Y=0.0, R=0.0, G=0.0, B=0.0, A=1.0;
	 	
	if(argc>0) X = atom_getfloat(&argv[0]);
	if(argc>1) Y = atom_getfloat(&argv[1]);
	if(argc>2) R = atom_getfloat(&argv[2]);
	if(argc>3) G = atom_getfloat(&argv[3]);
	if(argc>4) B = atom_getfloat(&argv[4]);
	if(argc>5) A = atom_getfloat(&argv[5]);
	
	ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		image->setColor(ofClamp(X, 0, image->getWidth()), ofClamp(Y, 0, image->getHeight()), 
			ofColor(R*255.0, G*255.0, B*255.0, A*255.0));
		px->image->needUpdate = true;
	} 
}

void pofimage_getcolor(void *x, t_float X, t_float Y)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	if(px->image == NULL) return;
	
	ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		t_atom ap[4];
		ofColor color = image->getColor(ofClamp(X, 0, image->getWidth()), ofClamp(Y, 0, image->getHeight()));
		SETFLOAT(&ap[0], color.r/255.0);
		SETFLOAT(&ap[1], color.g/255.0);
		SETFLOAT(&ap[2], color.b/255.0);
		SETFLOAT(&ap[3], color.a/255.0);
		outlet_anything(px->m_out2, s_color, 4, ap);
	} 
}

void pofimage_resize(void *x, t_float W, t_float H)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	//if(px->image == NULL) return;
	
	/*ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		image->resize(W, H);
		px->image->needUpdate = true;
	}*/
	px->resize.set(W, H);
}

void pofimage_crop(void *x, t_float X, t_float Y, t_float W, t_float H)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	/*if(px->image == NULL) return;
	
	ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		image->crop(X, Y, W, H);
	}*/
	px->crop.set(X, Y, W, H);
}

void pofimage_clear(void *x)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	if(px->image == NULL) return;
	
	ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		image->clear();
	}
}

void pofimage_grab(void *x, t_float X, t_float Y, t_float W, t_float H)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	/*if(px->image == NULL) return;
	
	ofImage *image = &px->image->im;
	if(image && px->image->loaded) {
		image->grabScreen(X, Y, W, H);
	}*/
	px->grab.set(X, Y, W, H);
}

void pofimage_grabfbo(void *x, t_symbol *fbo, t_float attachmentPoint)
{
	pofImage* px= (pofImage*)(((PdObject*)x)->parent);
	px->grabfbo = fbo;
}

void pofImage::setup(void)
{
	//post("pofimage_setup");
	s_set = gensym("set");
	s_saved = gensym("saved");
	s_size = gensym("size");
	s_monitor = gensym("monitor");
	s_color = gensym("color");
	pofimage_class = class_new(gensym("pofimage"), (t_newmethod)pofimage_new, (t_method)pofimage_free,
		sizeof(PdObject), 0, A_GIMME, A_NULL);
	POF_SETUP(pofimage_class);
	class_addmethod(pofimage_class, (t_method)pofimage_set, s_set, A_SYMBOL, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_save, gensym("save"), A_SYMBOL, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_reserve, gensym("reserve"), A_SYMBOL, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_unreserve, gensym("unreserve"), A_SYMBOL, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_monitor, gensym("setmonitor"), A_FLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_setcolor, gensym("setcolor"), A_GIMME, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_getcolor, gensym("getcolor"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_resize, gensym("resize"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_crop, gensym("crop"), A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_clear, gensym("clear"), A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_grab, gensym("grab"), A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_grabfbo, gensym("grabfbo"), A_SYMBOL, A_DEFFLOAT, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_out, s_size, A_GIMME, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_out, s_monitor, A_GIMME, A_NULL);
	class_addmethod(pofimage_class, (t_method)pofimage_out, s_saved, A_GIMME, A_NULL);
    imLoader = new pofImLoader;
    imLoaderHTTP = new pofImLoader;
	imLoader->startThread(true); //, false);    // blocking, non verbose
	imLoaderHTTP->startThread(true); //, false);    // blocking, non verbose
}

void pofImage::release(void)
{
	imLoader->waitForThread(true);
	delete imLoader;
	imLoaderHTTP->waitForThread(true);
	delete imLoaderHTTP;
}

void pofImage::Update()
{
	float w, h;
	t_atom ap[4];


	if(monitor) {
		unsigned int len = imLoader->getLen();
		unsigned int lenHTTP = imLoaderHTTP->getLen();
		unsigned int lenTotal = pofIm::getNumImages();
		if((len != loaderLen)||(lenHTTP != loaderLenHTTP)||(lenTotal != imgLen)) {
			loaderLen = len;
			loaderLenHTTP = lenHTTP;
			imgLen = lenTotal;
			SETSYMBOL(&ap[0], s_monitor);
			SETFLOAT(&ap[1], len);
			SETFLOAT(&ap[2], lenHTTP);
			SETFLOAT(&ap[3], lenTotal);
			queueToSelfPd(4, ap);
		}
	}
	
	if(file && (file != displayedFile)) {
		if(image) pofIm::letImage(image);
		//image = pofIm::getImage(file);
		image = pofIm::getImage(makefilename(file, pdcanvas));
		displayedFile = file;
	}
	if(image) {
		image->load();
		if(image->loaded) {
			if(!image->im.isAllocated()) 
			    image->im.allocate(width!=0?width:1, 
			        height!=0?height:width!=0?width:1, 
			        OF_IMAGE_COLOR_ALPHA);
            w = image->im.getWidth();
			h = image->im.getHeight();
		} else w = h = 0;
	} else w = h = 0;
	

	
	if(resize.y && image && image->loaded) {
		image->im.resize(resize.x, resize.y);
		resize.y = 0;
	}
	if(crop.height && image && image->loaded) {
		image->im.crop(crop.x, crop.y, crop.width, crop.height);
		crop.set(0, 0, 0, 0);
	}
	if(grab.height && image && image->loaded) {
		image->im.grabScreen(crop.x, crop.y, crop.width, crop.height);
		grab.set(0, 0, 0, 0);
	}
	
	if(image && (savefile!=NULL)) {
		image->im.save(savefile->s_name);
		SETSYMBOL(&ap[0], s_saved);
		SETSYMBOL(&ap[1], savefile);
		queueToSelfPd(2, ap);
		savefile = NULL;
	}
	
	if(image && grabfbo)
	{
		pofsubFbo* sub = pofsubFbo::get(grabfbo);
		sub->fbo.readToPixels(image->im.getPixels());
		pofsubFbo::let(sub);
		grabfbo = NULL;
		image->im.update();
	}

	if( (w != imWidth) || (h != imHeight) ) {
		SETSYMBOL(&ap[0], s_size);
		SETFLOAT(&ap[1], w);
		SETFLOAT(&ap[2], h);
		queueToSelfPd(3, ap);
		
		imWidth = w;
		imHeight = h;
	}

}

void pofImage::draw()
{
	float w = width, h = height;
	
	Update();
	
	if(image && image->loaded) {
		if(isTexture) image->bind();
		else {	
			if(w == 0) {
				if(h != 0) w = imWidth * h / imHeight;
				else w = imWidth;
			}
			if(h == 0) {
				if(w != 0) h = imHeight * w / imWidth;
				else h = imHeight; 
			}
			image->draw(0, 0, w, h);
		}
	}
}

void pofImage::postdraw()
{
	if(isTexture) image->unbind();
}

void pofImage::set(t_symbol *f)
{
	file = f ;//makefilename(f, pdcanvas);
}

void pofImage::save(t_symbol *f)
{
	savefile = f ;//makefilename(f, pdcanvas);
}

void pofImage::reserve(t_symbol *f)
{
	t_symbol *file = makefilename(f, pdcanvas);
	if(!file) return;
	
	pofIm* im = pofIm::getImage(file);
	im->load();
	
	std::list<pofIm*>::iterator it = reserved.begin();
	
	while(it != reserved.end()) {
		if(im == *it) break;
		it++;
	}
	
	if(it == reserved.end()) {
		reserved.push_back(im);
	} else pofIm::letImage(im);
}

void pofImage::unreserve(t_symbol *f)
{
	t_symbol *file = makefilename(f, pdcanvas);
	if(!file) return;
	pofIm *im = pofIm::findImage(file);
	if(im) {
		pofIm::letImage(im);
		reserved.remove(im);
	}	
}

void pofImage::unreserveAll()
{
	std::list<pofIm*>::iterator it = reserved.begin();
	int count;
	
	while(it != reserved.end()) {
		count = pofIm::letImage(*it);
		it++;
	}
	
	reserved.clear();
}


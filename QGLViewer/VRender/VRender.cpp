#ifdef WIN32
# include <windows.h>
#endif

#ifdef __APPLE__
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

#include <stdio.h>
#include <vector>
#include "VRender.h"
#include "ParserGL.h"
#include "Exporter.h"
#include "SortMethod.h"
#include "Optimizer.h"

using namespace vrender ;
using namespace std ;

void vrender::VectorialRender(RenderCB render_callback, void *callback_params, VRenderParams& vparams)
{
	GLfloat *feedbackBuffer = NULL ;
	SortMethod *sort_method = NULL ;
	Exporter *exporter = NULL ;

	try
	{
		GLint returned = -1 ;

		vparams.error() = 0 ;

		int nb_renders = 0 ;

		vparams.progress(0.0,string("Rendering")) ;

		while(returned < 0)
		{
			if(feedbackBuffer != NULL)
				delete[] feedbackBuffer ;

			feedbackBuffer = new GLfloat[vparams.size()] ;

			if(feedbackBuffer == NULL)
				throw std::runtime_error("Out of memory during feedback buffer allocation.") ;

			glFeedbackBuffer(vparams.size(), GL_3D_COLOR, feedbackBuffer);
			glRenderMode(GL_FEEDBACK);
			render_callback(callback_params);
			returned = glRenderMode(GL_RENDER);

			nb_renders++ ;

			if(returned < 0)
				vparams.size() *= 2 ;
		}

#ifdef A_VOIR
		if(SortMethod != EPS_DONT_SORT)
		{
			GLint depth_bits ;
			glGetIntegerv(GL_DEPTH_BITS, &depth_bits) ;

			EGALITY_EPS 		= 2.0/(1 << depth_bits) ;
			LINE_EGALITY_EPS 	= 2.0/(1 << depth_bits) ;
		}
#endif
		vparams.size() = max(int(returned),vparams.size()) ;
#ifdef _VRENDER_DEBUG
		cout << "Size = " << vparams.size() << ", returned=" << returned << endl ;
#endif

		//  On a un beau feedback buffer tout plein de saloperies. Faut aller
		// defricher tout ca. Ouaiiiis !

		vector<PtrPrimitive> primitive_tab ;

		ParserGL parserGL ;
		parserGL.parseFeedbackBuffer(feedbackBuffer,returned,primitive_tab,vparams) ;

		if(feedbackBuffer != NULL)
		{
			delete[] feedbackBuffer ;
			feedbackBuffer = NULL ;
		}

		if(vparams.isEnabled(VRenderParams::OptimizeBackFaceCulling))
		{
			BackFaceCullingOptimizer bfopt ;
			bfopt.optimize(primitive_tab,vparams) ;
		}

		// Lance la methode de sorting

		switch(vparams.sortMethod())
		{
			case VRenderParams::AdvancedTopologicalSort:
			case VRenderParams::TopologicalSort: {
																 TopologicalSortMethod *tsm = new TopologicalSortMethod() ;
																 tsm->setBreakCycles(vparams.sortMethod() == VRenderParams::AdvancedTopologicalSort) ;
																 sort_method = tsm ;
															 }
															 break ;

			case VRenderParams::BSPSort: 				sort_method = new BSPSortMethod() ;
																break ;

			case VRenderParams::NoSorting: 			sort_method = new DontSortMethod() ;
																break ;
			default:
																throw std::runtime_error("Unknown sorting method.") ;
		}

		sort_method->sortPrimitives(primitive_tab,vparams) ;

		// Lance les optimisations. L'ordre est important.

		if(vparams.isEnabled(VRenderParams::CullHiddenFaces))
		{
			VisibilityOptimizer vopt ;
			vopt.optimize(primitive_tab,vparams) ;
		}

#ifdef A_FAIRE
		if(vparams.isEnabled(VRenderParams::OptimizePrimitiveSplit))
		{
			PrimitiveSplitOptimizer psopt ;
			psopt.optimize(primitive_tab) ;
		}
#endif
		// Ecrit le fichier

		switch(vparams.format())
		{
			case VRenderParams::EPS: exporter = new EPSExporter() ;
											 break ;
			case VRenderParams::PS:  exporter = new PSExporter() ;
											 break ;
			case VRenderParams::XFIG:exporter = new FIGExporter() ;
											 break ;
#ifdef A_FAIRE
			case VRenderParams::SVG: exporter = new SVGExporter() ;
											 break ;
#endif
			default:
											 throw std::runtime_error("Sorry, this output format is not handled now. Only EPS and PS are currently supported.") ;
		}

		// sets background and black & white options

		GLfloat viewport[4],clearColor[4],lineWidth,pointSize ;

		glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
		glGetFloatv(GL_LINE_WIDTH, &lineWidth);
		glGetFloatv(GL_POINT_SIZE, &pointSize);
		glGetFloatv(GL_VIEWPORT, viewport);

		lineWidth /= (float)max(viewport[2] - viewport[0],viewport[3]-viewport[1]) ;

		// Sets which bounding box to use.

		if(vparams.isEnabled(VRenderParams::TightenBoundingBox))
			exporter->setBoundingBox(parserGL.xmin(),parserGL.ymin(),parserGL.xmax(),parserGL.ymax()) ;
		else
			exporter->setBoundingBox(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3]) ;

		exporter->setBlackAndWhite(vparams.isEnabled(VRenderParams::RenderBlackAndWhite)) ;
		exporter->setClearBackground(vparams.isEnabled(VRenderParams::AddBackground)) ;
		exporter->setClearColor(clearColor[0],clearColor[1],clearColor[2]) ;

		exporter->exportToFile(vparams.filename(),primitive_tab,vparams) ;

		// deletes primitives

		for(unsigned int i=0;i<primitive_tab.size();++i)
			delete primitive_tab[i] ;

		if(exporter != NULL) delete exporter ;
		if(sort_method != NULL) delete sort_method ;
	}
	catch(exception& e)
	{
		cout << "Render aborted: " << e.what() << endl ;

		if(exporter != NULL) delete exporter ;
		if(sort_method != NULL) delete sort_method ;
		if(feedbackBuffer != NULL) delete[] feedbackBuffer ;

		throw e ;
	}
}

VRenderParams::VRenderParams()
{
	_options = 0 ;
	_format = EPS ;
	_filename = NULL ;
	_progress_function = NULL ;
	_sortMethod = BSPSort ;
}

VRenderParams::~VRenderParams()
{
	if(_filename != NULL)
		free(_filename) ;
}


void VRenderParams::progress(float f, const std::string& progress_string)
{
	if(_progress_function != NULL)
		_progress_function(f,progress_string) ;
}

void VRenderParams::setFilename(const char *fn)
{
	if(strlen(fn) > 10000)
		throw std::runtime_error("VectorialRender: filename too long.") ;

	if(_filename != NULL)
		free(_filename) ;

	if((_filename = strdup(fn)) == NULL)
		throw std::runtime_error("could not copy supplied filename. Out of memory ?") ;
}

void VRenderParams::setOption(VRenderOption opt,bool b)
{
	if(b)
		_options |= opt ;
	else
		_options &= ~opt ;
}

bool VRenderParams::isEnabled(VRenderOption opt)
{
	return (_options & opt) > 0 ;
}

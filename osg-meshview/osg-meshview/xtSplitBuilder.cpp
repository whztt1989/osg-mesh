#include "StdAfx.h"
#include "xtSplitBuilder.h"
#include "xtCollisionEngine.h"
#include "xtGeometrySurfaceData.h"
#include "xtRayTriOverlay.h"
#include <algorithm>
#include "xtLog.h"
#include "./trianglev2/xtTrianglePLSG.h"

void xtCollisionEntity::DestroyMem()
{
	for ( size_t i=0; i<surfslot.size(); ++i ) {
		delete surfslot[i];
		surfslot[i] = NULL;
	}

	for ( size_t i=0; i<segslot.size(); ++i ) {
		delete segslot[i];
		segslot[i] = NULL;
	}
}

void xtCollisionEntity::InitializeCollisionEntity(xtGeometrySurfaceDataS *surf, std::vector<xtCollidePair> &pairs, xtSurfaceCat surfcat)
{
	for ( size_t i=0; i<pairs.size(); ++i ) {
		int idx;
		if ( XTSURFACEI==surfcat ) {
			idx = pairs[i].i;
		} else if ( XTSURFACEJ==surfcat ) {
			idx = pairs[i].j;
		} else {
			assert(false);
		}

		xtSurfaceSlot *temss = GetFaceSlotExit(idx);
		if ( temss ) {
			if ( XTSURFACEI==surfcat ) {
				temss->cflist.push_back(pairs[i].j);
			} else if ( XTSURFACEJ==surfcat ) {
				temss->cflist.push_back(pairs[i].i);
			} else {
				assert(false);
			}
			continue;
		}

		xtIndexTria3 &tria = surf->indices[idx];
		xtSurfaceSlot *ss = new xtSurfaceSlot;
		ss->idx = idx;
		ss->segs[0] = GetOrCreateSeg(tria.a[0], tria.a[1]);
		ss->segs[1] = GetOrCreateSeg(tria.a[1], tria.a[2]);
		ss->segs[2] = GetOrCreateSeg(tria.a[2], tria.a[0]);
		if ( XTSURFACEI==surfcat ) {
			ss->cflist.push_back(pairs[i].j);
		} else if ( XTSURFACEJ==surfcat ) {
			ss->cflist.push_back(pairs[i].i);
		} else {
			assert(false);
		}
		
		surfslot.push_back(ss);
	}
}

void xtCollisionEntity::AddSplitSegmentToFace(const int fi, xtSegment *seg)
{
	xtSurfaceSlot *ss = GetFaceSlotExit(fi);
	assert(ss);
	ss->segsonsurf.push_back(seg);
}

xtSurfaceSlot *xtCollisionEntity::GetFaceSlotExit(int fi)
{
	for ( size_t i=0; i<surfslot.size(); ++i ) {
		if ( surfslot[i]->idx==fi ) {
			return surfslot[i];
		}
	}
	return NULL;
}

xtSegmentSlot *xtCollisionEntity::GetOrCreateSeg(int i0, int i1)
{
	xtSegmentSlot *temp = SearchSeg(i0,i1);
	if ( temp ) {
		return temp;
	} else { // not exist create new one
		temp = new xtSegmentSlot;
		temp->startIdx = i0;
		temp->endIdx = i1;
		segslot.push_back(temp);
		return temp;
	}
}

xtSegmentSlot *xtCollisionEntity::SearchSeg(int i0, int i1)
{
	xtSegmentSlot *temp = NULL;
	for ( size_t i=0; i<segslot.size(); ++i ) {
		if ( ( segslot[i]->startIdx==i0&&segslot[i]->endIdx==i1 ||
			segslot[i]->startIdx==i1&&segslot[i]->endIdx==i0 ) ) {
			return segslot[i];
		}
	}

	return temp;
}


xtSplitBuilder::xtSplitBuilder(void)
{

}


xtSplitBuilder::~xtSplitBuilder(void)
{
	DestroyMem();
}

void xtSplitBuilder::SetCE(xtCollisionEngine *ce) 
{ 
	this->mCE = ce; 
	InitializeCollisionEntity();
}

void xtSplitBuilder::Split()
{
	SplitPnt(mPSI,mPSJ,mSFMI,mCE->mSurfI,mCE->mSurfJ);
	SplitPnt(mPSJ,mPSI,mSFMJ,mCE->mSurfJ,mCE->mSurfI);
	ConstructSplitSegments();
	TessellateCollidedFace(mPSI,mCE->mSurfI);
	TessellateCollidedFace(mPSJ,mCE->mSurfJ);
}

void xtSplitBuilder::SplitPnt(xtCollisionEntity *psI, xtCollisionEntity *psJ, xtSFMap &sfmap, xtGeometrySurfaceDataS *surfI, xtGeometrySurfaceDataS *surfJ)
{
	for ( size_t i=0; i<psI->surfslot.size(); ++i ) {
		xtSurfaceSlot *currssI = psI->surfslot[i];
		for ( size_t fidx=0; fidx<currssI->cflist.size(); ++fidx ) {
			const int collidessIdx = currssI->cflist[fidx];
			xtSurfaceSlot *collidess = psJ->GetFaceSlotExit(collidessIdx);
			for ( int eidx=0; eidx<3; ++eidx ) {
				xtSegmentSlot *colledseg = collidess->segs[eidx];
				const int startPntIdx = colledseg->startIdx;
				const int endPntIdx = colledseg->endIdx;
				// check start end collidessIdx as a key;
				xtSegmentFaceK key = { startPntIdx, endPntIdx, currssI->idx };
				xtSegmentFaceK keyinv = { endPntIdx, startPntIdx, currssI->idx };
				xtSFMap::iterator findkey = sfmap.find(key);
				if ( findkey!=sfmap.end() ) {
					xtVector3d *splitPnt = findkey->second;
					currssI->pointsOnSurf.push_back(splitPnt);
					colledseg->pointOnSeg.push_back(splitPnt);
				} else if ( (findkey=sfmap.find(keyinv))!=sfmap.end() ) {
					xtVector3d *splitPnt = findkey->second;
					currssI->pointsOnSurf.push_back(splitPnt);
					colledseg->pointOnSeg.push_back(splitPnt);
				} else { // calculate the new point and insert it to the sfmap
					xtVector3d startPntJ = GetWorldCoordinate(surfJ,startPntIdx);// surfJ->verts[startPntIdx];
					xtVector3d endPntJ   = GetWorldCoordinate(surfJ,endPntIdx); //surfJ->verts[endPntIdx];
					
					xtIndexTria3 &triaI = surfI->indices[currssI->idx];
					xtVector3d pa = GetWorldCoordinate(surfI,triaI.a[0]);//surfI->verts[triaI.a[0]];
					xtVector3d pb = GetWorldCoordinate(surfI,triaI.a[1]);//surfI->verts[triaI.a[1]];
					xtVector3d pc = GetWorldCoordinate(surfI,triaI.a[2]);//surfI->verts[triaI.a[2]];
					
					double t,u,v;
					xtVector3d dir = (endPntJ-startPntJ);
					const double dirlength = dir.norm();
					if ( dirlength < 0.00001 ) {
						printf("May De:%f\n",dirlength);
					}
					dir/=dirlength;
					//dir.normalize();
					//printf("Ray Direction x:%f,\ty:%f\tz:%f\n",dir.x(),dir.y(),dir.z());
					g_log_file << "++"<< eidx << "\t" <<  dir.x() << "\t" <<dir.y() << "\t" << dir.z() << '\n' ; 

					if ( IntersectTriangleTemplate(startPntJ, dir,pa,pb,pc,&t,&u,&v) ) {
						if ( dirlength>=t && t>=0) {
							xtVector3d *newsplitPnt = new xtVector3d(startPntJ+t*dir);
							mSharedSplitPoints.push_back(newsplitPnt);
							currssI->pointsOnSurf.push_back(newsplitPnt);
							colledseg->pointOnSeg.push_back(newsplitPnt);
							sfmap[key]=newsplitPnt;
							//printf("Point On Parameters %f\t%f\t%f\t\n",t,u,v);
							g_log_file << "--"<< eidx << "\t" << t << '\t' << u << '\t' << v << '\n';
#if XT_DEBUG_PERCE_POINT
							xtRaySegment rayseg = {startPntJ,*newsplitPnt,endPntJ};
							mDebugedge.push_back(rayseg);
#endif
						}
					}
				}
			}
		}
	}
}

xtOrderSegSuquence::~xtOrderSegSuquence()
{
	for ( size_t i=0; i<seq.size(); ++i ) {
		delete seq[i];
		seq[i] = NULL;
	} 
}

void xtOrderSegSuquence::OrderSequence(std::vector<xtSegment *> &segs) 
{
	xtSegmentVertexPointerKey *key;
	for ( size_t i=0; i<segs.size(); ++i ) {
		key = new xtSegmentVertexPointerKey;
		key->vert = segs[i]->seg0;
		key->vbseg = segs[i];
		seq.push_back(key);

		key = new xtSegmentVertexPointerKey;
		key->vert = segs[i]->seg1;
		key->vbseg = segs[i];
		seq.push_back(key);
	}
	std::sort(seq.begin(),seq.end(),xtSegmentVertexPointerKeyComp);

	const int seqsize = seq.size();
	std::vector<xtSegmentVertexPointerKey *> startend;
	std::vector<int> startendidx;
	int interseqcount = 0;
	for ( size_t i=0; i<seq.size()-1; ++i ) {
		xtSegmentVertexPointerKey  *right, *mid;
		mid = seq[i];
		right = seq[i+1];

		if ( right==mid ) {
			verts.push_back(mid->vert);
			sequence.push_back(interseqcount++);
			i++;
			continue;
		}

		if ( right!=mid ) {
			startend.push_back(mid);
			startendidx.push_back(i);
			continue;
		}
	}

	// concate the sequence;
	
}


void xtOrderSegSuquence::OrderSequenceSlow( std::vector<xtSegment *> &segs )
{
	std::vector<xtSegmentVertexPointerKey *> seq;
}


void xtSegmentPointer2Index::IndexPointer(std::vector<xtSegment *> &segs,
			std::vector<xtVector3d *> &verts,
		std::vector<std::tuple<int,int>> &segis)
{
	
	xtPntSlotMap vmap;
	int vertcount=0;
	for ( size_t i=0; i<segs.size(); ++i ) {
		std::tuple<int,int> intseg;
		xtSegment *seg = segs[i];
		xtPntSlotMap::iterator fsid = vmap.find( seg->seg0 );
		if ( fsid!=vmap.end() ) {
			std::get<0>(intseg) = fsid->second;
		} else {
			std::get<0>(intseg) = vertcount;
			vmap.insert( std::pair<xtVector3d*,int>(seg->seg0,vertcount++) );
		}
		xtPntSlotMap::iterator feid = vmap.find( seg->seg1 );
		if ( feid!=vmap.end() ) {
			std::get<1>( intseg ) = feid->second;
		} else {
			std::get<1>( intseg ) = vertcount;
			vmap.insert( std::pair<xtVector3d*,int>(seg->seg1,vertcount++) );
		}

		segis.push_back( intseg );
	}
	std::vector<int> idxcache;
	for ( xtPntSlotMap::iterator it=vmap.begin(); it!=vmap.end(); ++it ) {
		idxcache.push_back(it->second);
		verts.push_back(it->first);
	}

	std::sort( idxcache.begin(), idxcache.end() );

	for ( size_t i=0; i<idxcache.size()/2; ++i ) {
		std::swap( verts[i], verts[idxcache[i]] );
	}
}


void xtSplitBuilder::TessellateCollidedFace(xtCollisionEntity *ps, xtGeometrySurfaceDataS *surf)
{
	for ( size_t ssidx=0; ssidx<ps->surfslot.size(); ++ssidx ) {
		xtSurfaceSlot *ss = ps->surfslot[ssidx];
		
		std::vector<int> vertidxmap;

		xtIndexTria3 &triaI = surf->indices[ss->idx];
		xtVector3d pa = GetWorldCoordinate(surf,triaI.a[0]);//surfI->verts[triaI.a[0]];
		xtVector3d pb = GetWorldCoordinate(surf,triaI.a[1]);//surfI->verts[triaI.a[1]];
		xtVector3d pc = GetWorldCoordinate(surf,triaI.a[2]);//surfI->verts[triaI.a[2]];
		vertidxmap.push_back(triaI.a[0]);
		vertidxmap.push_back(triaI.a[1]);
		vertidxmap.push_back(triaI.a[2]);

		// construct local coordinate
		xtVector3d pba = pb - pa;
		xtVector3d pca = pc - pa;
		xtVector3d norm = pca.cross(pba);
		norm.normalize();
		xtVector3d xcoord = pca.cross(norm);
		xcoord.normalize();
		xtVector3d ycoord = norm.cross(xcoord);

		//xtMatrix3d rotm(
		//	xcoord.x(),ycoord.x(),norm.x(),
		//	xcoord.y(),ycoord.y(),norm.y(),
		//	xcoord.z(),ycoord.z(),norm.z());
		xtMatrix3d rotm;
		//rotm << xcoord.x(),ycoord.x(),norm.x(),
		//	xcoord.y(),ycoord.y(),norm.y(),
		//	xcoord.z(),ycoord.z(),norm.z() ;
		rotm << 
			xcoord.x(), xcoord.y(), xcoord.z(),
			ycoord.x(), ycoord.y(), ycoord.z(),
			norm.x(), norm.y(), norm.z();


		std::vector<xtVector3d *> &segonsurfverts = ss->pointsOnSurfVerbos;
		std::vector<std::tuple<int,int>> segonsurfindices;
		xtSegmentPointer2Index indexsegonsurf;
		indexsegonsurf.IndexPointer(ss->segsonsurf,segonsurfverts,segonsurfindices);
		
		std::vector<xtTriPnt2> verts2d;
		std::vector<xtSeg2WithMarker> segmarkerlist;
		std::vector<xtTriIndexO> outtris;

		// pack triangle boundary
		xtVector3d pa2d = rotm*pa;
		xtVector3d pb2d = rotm*pb;
		xtVector3d pc2d = rotm*pc;
		xtTriPnt2 a2d = { pa2d.x(), pa2d.y() };
		xtTriPnt2 b2d = { pb2d.x(), pb2d.y() };
		xtTriPnt2 c2d = { pc2d.x(), pc2d.y() };
		verts2d.push_back(a2d);
		verts2d.push_back(b2d);
		verts2d.push_back(c2d);
		xtSeg2WithMarker segmarker;
		segmarker.seg[0] = 0;
		segmarker.seg[1] = 1;
		segmarker.marker = 0;
		segmarkerlist.push_back(segmarker);
		segmarker.seg[0] = 1;
		segmarker.seg[1] = 2;
		segmarker.marker = 0;
		segmarkerlist.push_back(segmarker);
		segmarker.seg[0] = 2;
		segmarker.seg[1] = 0;
		segmarker.marker = 0;
		segmarkerlist.push_back(segmarker);

		// pack seg on triangle surf
		for ( size_t i=0; i<segonsurfverts.size(); ++i ) {
			xtVector3d p2d = rotm*(*(segonsurfverts[i]));
			xtTriPnt2 d2 = { p2d.x(), p2d.y() };
			verts2d.push_back(d2);
			vertidxmap.push_back(i);
		}
		xtSeg2WithMarker ontrisegmarker;
		ontrisegmarker.marker = 1;// one means seg on triangle surface
		for ( size_t i=0; i<segonsurfindices.size(); ++i ) {
			std::tuple<int,int> &segonsurf = segonsurfindices[i];
			ontrisegmarker.seg[0] = std::get<0>(segonsurf)+3;
			ontrisegmarker.seg[1] = std::get<1>(segonsurf)+3;
			segmarkerlist.push_back(ontrisegmarker);
		}

		xtTrianglePLSG splittriutil(verts2d, segmarkerlist, outtris);

		for ( size_t i=0; i<outtris.size(); ++i ) {
			xtIndexTria3 tria;
			for ( int fidx=0; fidx<3; ++fidx ) {
				tria.a[fidx] = outtris[i].idx[fidx];
			}
			ss->tris.push_back(tria);
		}
	}
}

bool counteriftrue( bool & istrue )
{
	return istrue;
}

void xtSplitBuilder::ConstructSplitSegments()
{
	for ( size_t ki=0; ki<mCE->mCollide.size(); ++ki ) {
		const int fIidx = mCE->mCollide[ki].i;
		const int fJidx = mCE->mCollide[ki].j;
		xtFaceFaceKey ffkey = {fIidx,fJidx};
		xtFaceFaceKey ffkeyinv = {fJidx, fIidx};
		xtFFMap::iterator findkey = mFFM.find(ffkey);
		// bug
		//xtFFMap::iterator findkeyinv = mFFM.find(ffkeyinv);
		if ( findkey!=mFFM.end() /*|| findkeyinv!=mFFM.end() */) continue;
		
		// let J's tri 3 edge test the I's face
		// 0) may degenerate
		// 1) one case in J 
		// 2) generate the common split segment
		std::vector<bool> edgestateI; edgestateI.reserve(3);
		std::vector<xtVector3d *> spIlist; spIlist.reserve(3);
		std::vector<bool> edgestateJ; edgestateJ.reserve(3);
		std::vector<xtVector3d *> spJlist; spJlist.reserve(3);

		xtIndexTria3 triaI = mCE->mSurfI->indices[fIidx];
		xtIndexTria3 triaJ = mCE->mSurfJ->indices[fJidx];

		// check start end collidessIdx as a key;
		for ( int i=0; i<3; ++i ) {
			xtSegmentFaceK key = { triaJ.a[i], triaJ.a[(i+1)%3], fIidx };
			xtSegmentFaceK keyinv = { triaJ.a[(i+1)%3], triaJ.a[i], fIidx };
			xtSFMap::iterator findit;
			if ( (findit=mSFMI.find(key))!=mSFMI.end() ) {
				edgestateI.push_back(true);
				spIlist.push_back(findit->second);
			} else if ( (findit=mSFMI.find(keyinv))!=mSFMI.end() ) {
				edgestateI.push_back(true);
				spIlist.push_back(findit->second);
			} else {
				edgestateI.push_back(false);
				spIlist.push_back(NULL);
			}
		}

		for ( int i=0; i<3; ++i ) {
			xtSegmentFaceK key = { triaI.a[i], triaI.a[(i+1)%3], fJidx };
			xtSegmentFaceK keyinv = { triaI.a[(i+1)%3], triaI.a[i], fJidx };
			xtSFMap::iterator findit;
			if ( (findit=mSFMJ.find(key))!=mSFMJ.end() ) {
				edgestateJ.push_back(true);
				spJlist.push_back(findit->second);
			} else if ( (findit=mSFMJ.find(keyinv))!=mSFMJ.end() ) {
				edgestateJ.push_back(true);
				spJlist.push_back(findit->second);
			} else {
				edgestateJ.push_back(false);
				spJlist.push_back(NULL);
			}
		}

		//int numIntersectWI, numIntersectWJ;
		const int numIntersectWI = std::count(edgestateI.begin(),edgestateI.end(),true);
		const int numIntersectWJ = std::count(edgestateJ.begin(),edgestateJ.end(),true);
		if ( 0==numIntersectWI && 2==numIntersectWJ ) {
			//std::vector<bool>::iterator findfalse = std::find(edgestateI.begin(),edgestateI.end(),false);
			int falseIdxJ;
			for ( size_t i=0; i<edgestateJ.size(); ++i ) {
				if ( !edgestateJ[i] ) {
					falseIdxJ = i;
				}
			}
			xtSegment *newseg = new xtSegment;
			newseg->seg0 = spJlist[(falseIdxJ+1)%3];
			newseg->seg1 = spJlist[(falseIdxJ+2)%3];
			mSharedSplitSegList.push_back(newseg);

			mPSI->AddSplitSegmentToFace(fIidx,newseg);
			mPSJ->AddSplitSegmentToFace(fJidx,newseg);
			mFFM[ffkey] = newseg;

		} else if ( 1==numIntersectWI&&1==numIntersectWJ ) {
			std::vector<bool>::iterator iiiter = std::find(edgestateI.begin(), edgestateI.end(), true);
			const size_t iiidx = std::distance(edgestateI.begin(),iiiter);

			std::vector<bool>::iterator ijiter = std::find(edgestateJ.begin(), edgestateJ.end(), true);
			const size_t ijidx = std::distance(edgestateJ.begin(),ijiter);

			xtSegment *newseg = new xtSegment;
			newseg->seg0 = spIlist[iiidx];
			newseg->seg1 = spJlist[ijidx];
			mSharedSplitSegList.push_back(newseg);

			mPSI->AddSplitSegmentToFace(fIidx,newseg);
			mPSJ->AddSplitSegmentToFace(fJidx,newseg);
			mFFM[ffkey] = newseg;
			
		} else if ( 2==numIntersectWI && 0==numIntersectWJ ) {
			int falseIdx;
			for ( size_t i=0; i<edgestateI.size(); ++i ) {
				if ( !edgestateI[i] ) {
					falseIdx = i;
				}
			}
			xtSegment *newseg = new xtSegment;
			newseg->seg0 = spIlist[(falseIdx+1)%3];
			newseg->seg1 = spIlist[(falseIdx+2)%3];
			mSharedSplitSegList.push_back(newseg);

			mPSI->AddSplitSegmentToFace(fIidx,newseg);
			mPSJ->AddSplitSegmentToFace(fJidx,newseg);
			mFFM[ffkey] = newseg;
			
		} else if ( 3==numIntersectWI&&0==numIntersectWJ) {
			printf( "Co point I\n" );

		} else if ( 0==numIntersectWI&&3==numIntersectWJ) {
			printf( "Co point J\n" );

		} else if ( 2==numIntersectWI&&1==numIntersectWJ) {
			//printf( "I touch on\n" ); 
			// one point of I on triangle J
			// There at least two edge intersect with J degenerate case
			std::vector<bool>::iterator findIit = std::find(edgestateI.begin(),edgestateI.end(),false);
			const size_t falseIidx = std::distance(edgestateI.begin(),findIit);
			
			std::vector<bool>::iterator findJit = std::find(edgestateJ.begin(),edgestateJ.end(),true);
			const size_t trueJidx = std::distance(edgestateJ.begin(),findJit);

			xtSegment *newseg = new xtSegment;
			newseg->seg0 = spIlist[(falseIidx+1)%3];
			newseg->seg1 = spJlist[trueJidx];
			mSharedSplitSegList.push_back(newseg);

			mPSI->AddSplitSegmentToFace(fIidx,newseg);
			mPSJ->AddSplitSegmentToFace(fJidx,newseg);
			mFFM[ffkey] = newseg;

		} else if ( 1==numIntersectWI&&2==numIntersectWJ) {
			//printf( "J touch on\n" );
			// one point of J on triangle area I
			// There at lease two edge intersect by this degenerate case
			// In this branch just one edge collision
			std::vector<bool>::iterator findIit = std::find(edgestateI.begin(),edgestateI.end(),true);
			const size_t trueIidx = std::distance(edgestateI.begin(),findIit);
			
			std::vector<bool>::iterator findJit = std::find(edgestateJ.begin(),edgestateJ.end(),false);
			const size_t falseJidx = std::distance(edgestateJ.begin(),findJit);

			xtSegment *newseg = new xtSegment;
			newseg->seg0 = spIlist[trueIidx];
			newseg->seg1 = spJlist[(falseJidx+1)%3];
			mSharedSplitSegList.push_back(newseg);

			mPSI->AddSplitSegmentToFace(fIidx,newseg);
			mPSJ->AddSplitSegmentToFace(fJidx,newseg);
			mFFM[ffkey] = newseg;

		} else if ( 3==numIntersectWI ) {
			// impossible if they are not lay int the same plane
			assert(false);
		}  else {
			// unknow situation
			printf( "I\t%d, J\t%d\n",numIntersectWI,numIntersectWJ);
			assert(false);
		}


	}
}

void xtSplitBuilder::InitializeCollisionEntity()
{
	mPSI = new xtCollisionEntity;
	mPSJ = new xtCollisionEntity;
	mPSI->InitializeCollisionEntity(mCE->mSurfI,mCE->mCollide,XTSURFACEI);
	mPSJ->InitializeCollisionEntity(mCE->mSurfJ,mCE->mCollide,XTSURFACEJ);
}



void xtSplitBuilder::DestroyMem()
{
	delete mPSI;
	mPSI = NULL;
	delete mPSJ;
	mPSJ = NULL;

	for ( size_t i=0; i<mSharedSplitSegList.size(); ++i ) {
		delete mSharedSplitSegList[i];
		mSharedSplitSegList[i] = NULL;
	}

	for ( size_t i=0; i<mSharedSplitPoints.size(); ++i ) {
		delete mSharedSplitPoints[i];
		mSharedSplitPoints[i] = NULL;
	}

}

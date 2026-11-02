#include "CarMesh.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
CarMesh CarMesh::load(const QString&path,bool zUp){
 if(QFileInfo(path).size()>32*1024*1024)throw std::invalid_argument("Mesh file exceeds 32 MiB");
 Assimp::Importer importer;importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY,true);auto *scene=importer.ReadFile(path.toStdString(),aiProcess_Triangulate|aiProcess_PreTransformVertices|aiProcess_JoinIdenticalVertices|aiProcess_ValidateDataStructure);
 if(!scene||!scene->HasMeshes())throw std::invalid_argument(std::string("Mesh import failed: ")+importer.GetErrorString());
 size_t count=0;QVector3D low(1e30f,1e30f,1e30f),high(-1e30f,-1e30f,-1e30f);
 for(unsigned m=0;m<scene->mNumMeshes;++m){auto*mesh=scene->mMeshes[m];count+=mesh->mNumFaces;if(count>20000)throw std::invalid_argument("Mesh has more than 20,000 faces; simplify before import");for(unsigned i=0;i<mesh->mNumVertices;++i){const auto&v=mesh->mVertices[i];if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::invalid_argument("Non-finite mesh geometry");low.setX(std::min(low.x(),v.x));low.setY(std::min(low.y(),v.y));low.setZ(std::min(low.z(),v.z));high.setX(std::max(high.x(),v.x));high.setY(std::max(high.y(),v.y));high.setZ(std::max(high.z(),v.z));}}
 auto extent=high-low;bool longX=zUp?extent.x()>extent.y():extent.x()>extent.z();float length=longX?extent.x():zUp?extent.y():extent.z();if(length<1e-6f)throw std::invalid_argument("Degenerate mesh dimensions");
 double frontSum=0;size_t frontCount=0;for(unsigned m=0;m<scene->mNumMeshes;++m){auto*part=scene->mMeshes[m];auto name=QString::fromUtf8(part->mName.C_Str()).toLower();if(name.contains("front")&&name.contains("wheel"))for(unsigned i=0;i<part->mNumVertices;++i){frontSum+=longX?part->mVertices[i].x:zUp?part->mVertices[i].y:part->mVertices[i].z;++frontCount;}}
 const bool flipForward=frontCount&&frontSum/frontCount<(longX?(low.x()+high.x()):(zUp?low.y()+high.y():low.z()+high.z()))*.5;
 auto transform=[&](const aiVector3D&v){auto centre=(low+high)*.5f;QVector3D out=zUp?(longX?QVector3D(v.x-centre.x(),v.y-centre.y(),v.z-low.z()):QVector3D(v.y-centre.y(),-v.x+centre.x(),v.z-low.z())):longX?QVector3D(v.x-centre.x(),v.z-centre.z(),v.y-low.y()):QVector3D(v.z-centre.z(),-v.x+centre.x(),v.y-low.y());if(flipForward){out.setX(-out.x());out.setY(-out.y());}return out*(4.5f/length);};
 CarMesh result;result.source=QFileInfo(path).fileName();result.triangles.reserve(count);
 for(unsigned m=0;m<scene->mNumMeshes;++m){auto*mesh=scene->mMeshes[m];aiColor3D base(.2f,.65f,.7f);QImage texture;
 if(mesh->mMaterialIndex<scene->mNumMaterials){auto*mat=scene->mMaterials[mesh->mMaterialIndex];mat->Get(AI_MATKEY_COLOR_DIFFUSE,base);aiString file;if(mat->GetTexture(aiTextureType_DIFFUSE,0,&file)==AI_SUCCESS){if(const auto*embedded=scene->GetEmbeddedTexture(file.C_Str())){if(embedded->mHeight==0)texture.loadFromData(reinterpret_cast<const uchar*>(embedded->pcData),embedded->mWidth);}else{auto name=QString::fromUtf8(file.C_Str()).replace('\\','/');texture.load(QFileInfo(path).dir().filePath(name));}}}
 QVector3D partMin(1e30f,1e30f,1e30f),partMax(-1e30f,-1e30f,-1e30f);
 for(unsigned j=0;j<mesh->mNumVertices;++j){auto v=transform(mesh->mVertices[j]);for(int k=0;k<3;++k){partMin[k]=std::min(partMin[k],v[k]);partMax[k]=std::max(partMax[k],v[k]);}}
 auto pivot=(partMin+partMax)*.5f;auto part=QString::fromUtf8(mesh->mName.C_Str()).toLower();
 for(unsigned i=0;i<mesh->mNumFaces;++i){const auto&face=mesh->mFaces[i];if(face.mNumIndices!=3)continue;auto color=QColor::fromRgbF(std::clamp(base.r,0.f,1.f),std::clamp(base.g,0.f,1.f),std::clamp(base.b,0.f,1.f));if(!texture.isNull()&&mesh->HasTextureCoords(0)){auto uv=(mesh->mTextureCoords[0][face.mIndices[0]]+mesh->mTextureCoords[0][face.mIndices[1]]+mesh->mTextureCoords[0][face.mIndices[2]])/3.f;color=texture.pixelColor(std::clamp(int(uv.x*texture.width()),0,texture.width()-1),std::clamp(int((1-uv.y)*texture.height()),0,texture.height()-1));}result.triangles.push_back({transform(mesh->mVertices[face.mIndices[0]]),transform(mesh->mVertices[face.mIndices[1]]),transform(mesh->mVertices[face.mIndices[2]]),color,pivot,part.contains("wheel"),part.contains("front")});}}
 if(result.triangles.empty())throw std::invalid_argument("Mesh contains no triangles");
 return result;
}

nlohmann::json CarMesh::toJson()const{auto rows=nlohmann::json::array();for(const auto&t:triangles)rows.push_back({t.a.x(),t.a.y(),t.a.z(),t.b.x(),t.b.y(),t.b.z(),t.c.x(),t.c.y(),t.c.z(),t.color.red(),t.color.green(),t.color.blue(),t.pivot.x(),t.pivot.y(),t.pivot.z(),t.wheel?1:0,t.front?1:0});return {{"name",source.toStdString()},{"triangles",rows}};}
CarMesh CarMesh::fromJson(const nlohmann::json&data){CarMesh result;result.source=QString::fromStdString(data.at("name"));const auto&rows=data.at("triangles");if(!rows.is_array()||rows.empty()||rows.size()>20000)throw std::invalid_argument("Invalid mesh triangle count");for(const auto&r:rows){if(r.size()!=17)throw std::invalid_argument("Invalid stored mesh");for(const auto&n:r)if(!n.is_number()||!std::isfinite(n.get<double>())||std::abs(n.get<double>())>1000)throw std::invalid_argument("Invalid mesh coordinates");result.triangles.push_back({{r[0],r[1],r[2]},{r[3],r[4],r[5]},{r[6],r[7],r[8]},QColor(r[9].get<int>(),r[10].get<int>(),r[11].get<int>()),{r[12],r[13],r[14]},r[15].get<int>()!=0,r[16].get<int>()!=0});}return result;}

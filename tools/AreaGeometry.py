"""Mod-owned Cursed Arena geometry. Metres, Y-up; physics winding."""
import math
import numpy as np
from shapely import Polygon, constrained_delaunay_triangles, orient_polygons
from shapely.ops import nearest_points
from shapely.geometry import Point

RADIUS = 24.0
PAD_RADIUS = 3.75
RAMP_HALF_WIDTH = 1.5
RAMP_END = 29.5
PAD_Z = RAMP_END + math.sqrt(PAD_RADIUS**2 - RAMP_HALF_WIDTH**2)
PAD_HEIGHT = 1.8
BONFIRE_LOCAL = np.array([0., PAD_HEIGHT, PAD_Z + .4])
ARRIVAL_LOCAL = np.array([0., PAD_HEIGHT + .05, PAD_Z - .8])
LAVA_Y = -3.2
DROP_DISTANCE = 28.5
RIM_HEIGHT = 1.3
BARRIER_HEIGHT = 8.0


def surface_height(z):
    return PAD_HEIGHT * min(1., max(0., (z - RADIUS) / (RAMP_END - RADIUS)))


def floor_polygons():
    join_z=math.sqrt(RADIUS**2-RAMP_HALF_WIDTH**2)
    join_angle=math.atan2(join_z,RAMP_HALF_WIDTH)
    arc=[(RADIUS*math.cos(a),RADIUS*math.sin(a)) for a in np.linspace(join_angle, -math.pi-join_angle, 129)]
    # Exact shared 3 m chords keep the rectangle attached to both circles.
    arc[0]=(RAMP_HALF_WIDTH,join_z);arc[-1]=(-RAMP_HALF_WIDTH,join_z)
    main=Polygon(arc+[(-RAMP_HALF_WIDTH,RADIUS),(RAMP_HALF_WIDTH,RADIUS)])
    ramp=Polygon([(-RAMP_HALF_WIDTH,RADIUS),(-RAMP_HALF_WIDTH,RAMP_END),(RAMP_HALF_WIDTH,RAMP_END),(RAMP_HALF_WIDTH,RADIUS)])
    a=math.atan2(RAMP_END-PAD_Z,-RAMP_HALF_WIDTH)
    b=math.atan2(RAMP_END-PAD_Z,RAMP_HALF_WIDTH)-math.tau
    cap=[(PAD_RADIUS*math.cos(t),PAD_Z+PAD_RADIUS*math.sin(t)) for t in np.linspace(a,b,97)]
    cap[0]=(-RAMP_HALF_WIDTH,RAMP_END);cap[-1]=(RAMP_HALF_WIDTH,RAMP_END)
    return main,ramp,Polygon(cap)


def outline():
    main,ramp,pad=floor_polygons()
    result=main.union(ramp).union(pad)
    assert result.geom_type=='Polygon' and result.is_valid and not result.interiors
    return result


def contour(poly):
    return np.asarray(orient_polygons(poly,exterior_cw=True).exterior.coords[:-1],dtype=np.float64)


class Mesh:
    def __init__(self):self.vertices=[];self.faces=[];self.lookup={}
    def vertex(self,x,y,z):
        key=tuple(round(float(a),6) for a in (x,y,z))
        if key not in self.lookup:self.lookup[key]=len(self.vertices);self.vertices.append([x,y,z])
        return self.lookup[key]
    def face(self,a,b,c):
        if len({a,b,c})==3:self.faces.append([a,b,c])
    def plane(self,poly,height,up=True):
        for tri in constrained_delaunay_triangles(poly).geoms:
            coords=contour(tri)
            ids=[self.vertex(x,height(x,z),z) for x,z in coords]
            self.face(*(ids if up else ids[::-1]))
    def wall(self,poly,lower,upper,inward=False):
        ring=contour(poly)
        lo=[self.vertex(x,lower(x,z),z) for x,z in ring]
        hi=[self.vertex(x,upper(x,z),z) for x,z in ring]
        for i in range(len(ring)):
            j=(i+1)%len(ring)
            fs=[(lo[i],hi[j],hi[i]),(lo[i],lo[j],hi[j])]
            for f in fs:self.face(*(f[::-1] if inward else f))
    def arrays(self):return np.asarray(self.vertices,dtype=np.float32),np.asarray(self.faces,dtype=np.uint32)


def island():
    shape=outline();mesh=Mesh()
    main,ramp,pad=floor_polygons()
    mesh.plane(main,lambda x,z:0.)
    mesh.plane(ramp,lambda x,z:surface_height(z))
    mesh.plane(pad,lambda x,z:PAD_HEIGHT)
    walk_faces=np.asarray(mesh.faces,dtype=np.uint32)
    # Derive the whole rim from the real concave outline, including both
    # junctions. Buffering avoids gaps or crossing strips beside the ramp.
    boundary=shape.boundary
    def edge_height(x,z):return surface_height(nearest_points(boundary,Point(x,z))[0].y)
    previous=shape;previous_y=lambda x,z:edge_height(x,z)
    for offset,height in [(.15,RIM_HEIGHT),(.7,RIM_HEIGHT),(1.4,1.1),(3.8,-3.8)]:
        outer=shape.buffer(offset,quad_segs=4)
        region=outer.difference(previous)
        # Every annular triangle has vertices on its two boundary loops.
        inner_line=previous.boundary;outer_line=outer.boundary
        def y(x,z,inner=inner_line,out=outer_line,old=previous_y,h=height):
            p=Point(x,z)
            if inner.distance(p)<out.distance(p):return old(x,z)
            return h if h<0 else edge_height(x,z)+h
        mesh.plane(region,y)
        previous=outer
        previous_y=(lambda x,z,h=height: h if h<0 else edge_height(x,z)+h)
    mesh.wall(previous,lambda x,z:-5.5,lambda x,z:-3.8)
    mesh.plane(previous,lambda x,z:-5.5,up=False)
    v,f=mesh.arrays()
    return v,f,walk_faces,shape


def barrier(shape):
    """A closed collision-only ring behind the visible rim, well above a jump."""
    inner=shape.buffer(.48,quad_segs=4);outer=shape.buffer(.65,quad_segs=4)
    boundary=shape.boundary
    def base(x,z):return surface_height(nearest_points(boundary,Point(x,z))[0].y)
    lo=lambda x,z:base(x,z)-1.
    hi=lambda x,z:base(x,z)+BARRIER_HEIGHT
    mesh=Mesh()
    mesh.wall(inner,lo,hi,inward=True);mesh.wall(outer,lo,hi)
    shell=outer.difference(inner)
    mesh.plane(shell,hi);mesh.plane(shell,lo,up=False)
    return mesh.arrays()


def lava(shape,falling=False):
    mesh=Mesh()
    if not falling:
        inner=shape.buffer(3.,quad_segs=4)
        for offset in [6.,9.,12.,16.,20.,24.,DROP_DISTANCE]:
            outer=shape.buffer(offset,quad_segs=4)
            mesh.plane(outer.difference(inner),lambda x,z:LAVA_Y)
            inner=outer
    else:
        # Matching edge coordinates close the seam; the fall fades into void.
        ring=contour(shape.buffer(DROP_DISTANCE,quad_segs=4))
        old=None
        for scale,y in [(1.,LAVA_Y),(1.001,-8.),(1.003,-15.),(1.005,-28.)]:
            ids=[mesh.vertex(x*scale,y,z*scale) for x,z in ring]
            if old is not None:
                for i in range(len(ids)):
                    j=(i+1)%len(ids)
                    mesh.face(old[i],ids[i],ids[j]);mesh.face(old[i],ids[j],old[j])
            old=ids
    return mesh.arrays()


def boulder(index):
    """Closed basalt boulders using existing rock materials."""
    n=10;points=[];faces=[]
    angle=index*math.tau/10+.12*math.sin(index*1.7)
    radius=RADIUS+6.6+(index%3)*.45
    # Keep the northern boulders clear of the larger bonfire platform.
    centre=np.array([radius*math.cos(angle),-3.9,radius*math.sin(angle)])
    if centre[2]>24 and abs(centre[0])<8:centre[0]+=9
    sx=.65+(index%3)*.16;sz=.7+(index%4)*.12;height=2.+(index%4)*.55
    points.append([0.,0.,0.])
    for ring,(r,y) in enumerate([(.7,0.),(1.,.35),(.65,.8)]):
        for i in range(n):
            t=i*math.tau/n;rough=1+.13*math.sin(i*2.3+index+ring*.5)
            points.append([sx*r*rough*math.cos(t),height*y,sz*r*rough*math.sin(t)])
    top=len(points);points.append([sx*.1,height,sz*.05])
    for i in range(n):
        j=(i+1)%n;faces.append([0,1+i,1+j])
        for ring in range(2):
            a=1+ring*n+i;b=1+ring*n+j;c=a+n;d=b+n
            faces.extend([[a,b,d],[a,d,c]])
        faces.append([top,1+2*n+j,1+2*n+i])
    return np.array(points,dtype=np.float32)+centre,np.array(faces,dtype=np.uint32)

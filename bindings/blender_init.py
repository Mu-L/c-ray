
bl_info = {
	"name": "c-ray integration for Blender",
	"author": "Valtteri Koskivuori",
	"blender": (2, 80, 0),
	"description": "Experimenting with the new c-ray Python API",
	"doc_url": "https://github.com/vkoskiv/c-ray",
	"tracker_url": "",
	"category": "Render"
}

if "bpy" in locals():
	import importlib
	if "c_ray" in locals():
		importlib.reload(c_ray)
	if "properties" in locals():
		importlib.reload(properties)
	if "ui" in locals():
		importlib.reload(ui)

import bpy
import numpy as np
import time
import datetime
import gpu
from gpu_extras.presets import draw_texture_2d
import threading

from . import (
	c_ray
)

import ctypes as ct
from array import array
import math
import mathutils

from . nodes.color import (
	cr_color
)

from . nodes.convert import *

from bpy.props import (
	IntProperty,
	PointerProperty,
	StringProperty,
)

class CrayRenderSettings(bpy.types.PropertyGroup):
	samples: IntProperty(
		name="Samples",
		description="Number of samples to render for each pixel",
		min=1, max=(1 << 24),
		default=32,
	)
	threads: IntProperty(
		name="Threads",
		description="Number of threads to use for rendering",
		min=1, max=256,
		default=4,
	)
	tile_size: IntProperty(
		name="Tile Size",
		description="Size of tile for each thread",
		min=0, max=1024,
		default=32,
	)
	bounces: IntProperty(
		name="Bounces",
		description="Max number of bounces for each light path",
		min=0, max=1024,
		default=16,
	)
	node_list: StringProperty(
		name="Worker node list",
		description="comma-separated list of IP:port pairs",
		maxlen=4096,
		default=""
	)
	@classmethod
	def register(cls):
		bpy.types.Scene.c_ray = PointerProperty(
			name="c-ray Render Settings",
			description="c-ray Render Settings",
			type=cls,
		)

	@classmethod
	def unregister(cls):
		del bpy.types.Scene.c_ray

def mesh_triangulate(mesh):
	import bmesh
	bm = bmesh.new();
	bm.from_mesh(mesh)
	bmesh.ops.triangulate(bm, faces=bm.faces)
	bm.to_mesh(mesh)
	bm.free()

# TODO: Probably a better way to do this
def to_cr_matrix(matrix):
	cr_mtx = c_ray.cr_matrix()
	cr_mtx.mtx[0] = matrix[0][0]
	cr_mtx.mtx[1] = matrix[0][1]
	cr_mtx.mtx[2] = matrix[0][2]
	cr_mtx.mtx[3] = matrix[0][3]
	cr_mtx.mtx[4] = matrix[1][0]
	cr_mtx.mtx[5] = matrix[1][1]
	cr_mtx.mtx[6] = matrix[1][2]
	cr_mtx.mtx[7] = matrix[1][3]
	cr_mtx.mtx[8] = matrix[2][0]
	cr_mtx.mtx[9] = matrix[2][1]
	cr_mtx.mtx[10] = matrix[2][2]
	cr_mtx.mtx[11] = matrix[2][3]
	cr_mtx.mtx[12] = matrix[3][0]
	cr_mtx.mtx[13] = matrix[3][1]
	cr_mtx.mtx[14] = matrix[3][2]
	cr_mtx.mtx[15] = matrix[3][3]
	return cr_mtx

# TODO: Normals & tex coords
def to_cr_face(me, poly):
	indices = []
	for loop_idx in range(poly.loop_start, poly.loop_start + poly.loop_total):
		indices.append(me.loops[loop_idx].vertex_index)
	cr_face = c_ray.cr_face()
	cr_face.vertex_idx[:] = indices
	cr_face.mat_idx = poly.material_index
	cr_face.has_normals = 0
	return cr_face

def cr_vertex_buf(scene, me):
	verts = []
	for v in me.vertices:
		cr_vert = c_ray.cr_vector()
		cr_vert.x = v.co[0]
		cr_vert.y = v.co[1]
		cr_vert.z = v.co[2]
		verts.append(cr_vert)
	normals = []
	texcoords = []
	vbuf = (c_ray.cr_vector * len(verts))(*verts)
	nbuf = (c_ray.cr_vector * len(normals))(*normals)
	tbuf = (c_ray.cr_coord  * len(texcoords))(*texcoords)
	print("new vbuf: v: {}, n: {}, t: {}".format(len(verts), len(normals), len(texcoords)))
	cr_vbuf = scene.vertex_buf_new(bytearray(vbuf), len(verts), bytearray(nbuf), len(normals), bytearray(tbuf), len(texcoords))
	return cr_vbuf

def dump(obj):
	for attr in dir(obj):
		if hasattr(obj, attr):
			print("obj.{} = {}".format(attr, getattr(obj, attr)))

def on_start(renderer_cb_info, user_data):
	print("on_start called")

def on_stop(renderer_cb_info, user_data):
	print("on_stop called")

def on_status_update(cb_info, args):
	cr_renderer, update_progress, update_stats, test_break = args
	update_stats("", "ETA: {}".format(str(datetime.timedelta(milliseconds=cb_info.eta_ms))))
	update_progress(cb_info.completion)
	if test_break():
		print("Stopping c-ray")
		cr_renderer.stop()

def draw_direct(bitmap):
	if not bitmap:
		return
	float_count = bitmap.width * bitmap.height * bitmap.stride
	buffer_from_memory = ct.pythonapi.PyMemoryView_FromMemory
	buffer_from_memory.restype = ct.py_object
	buffer = buffer_from_memory(bitmap.data.float_ptr, 4 * float_count)
	pixels = np.frombuffer(buffer, np.float32)
	pixels = gpu.types.Buffer('FLOAT', float_count, pixels)
	texture = None
	try:
		texture = gpu.types.GPUTexture((bitmap.width, bitmap.height), format='RGBA32F', data=pixels)
	except ValueError as error:
		print("Texture creation didn't work: {} (width: {}, height: {})".format(error, bitmap.width, bitmap.height))
	if texture:
		draw_texture_2d(texture, (0, 0), texture.width, texture.height)

def status_update_interactive(cb_info, args):
	tag_redraw, update_stats, self = args
	tag_redraw()
	if cb_info.finished_passes == self.cr_renderer.prefs.samples:
		update_stats("Rendering done", "")
	else:
		update_stats("Sample {}".format(cb_info.finished_passes), "")

class CrayRender(bpy.types.RenderEngine):
	bl_idname = "C_RAY"
	bl_label = "c-ray for Blender"
	bl_use_preview = True
	bl_use_shading_nodes_custom = False
	cr_interactive_running = False
	cr_renderer = None
	old_mtx = None
	old_dims = None
	old_zoom = None

	def __init__(self):
		print("c-ray initialized")
		self.cr_scene = None
		self.cr_interactive_running = False
		c_ray.log_level_set(c_ray.log_level.Debug)

	def __del__(self):
		if self.cr_interactive_running:
			self.cr_renderer.stop()
			self.cr_interactive_running = False
		if self.cr_renderer:
			self.cr_renderer.close()
		print("c-ray deleted")

	def sync_scene(self, depsgraph):
		b_scene = depsgraph.scene
		objects = depsgraph.objects
		# Sync cameras
		for idx, ob_main in enumerate(objects):
			if ob_main.type != 'CAMERA':
				continue

			bl_cam = ob_main.data

			cr_cam = None
			if bl_cam.name in self.cr_scene.cameras:
				cr_cam = self.cr_scene.cameras[bl_cam.name]
			else:
				cr_cam = self.cr_scene.camera_new(ob_main.name)
			mtx = ob_main.matrix_world
			euler = mtx.to_euler('XYZ')
			loc = mtx.to_translation()
			cr_cam.opts.fov = math.degrees(bl_cam.angle)
			cr_cam.opts.pose_x = loc[0]
			cr_cam.opts.pose_y = loc[1]
			cr_cam.opts.pose_z = loc[2]
			cr_cam.opts.pose_roll = euler[0]
			cr_cam.opts.pose_pitch = euler[1]
			cr_cam.opts.pose_yaw = euler[2]

			scale = b_scene.render.resolution_percentage / 100.0
			size_x = int(b_scene.render.resolution_x * scale)
			size_y = int(b_scene.render.resolution_y * scale)
			cr_cam.opts.res_x = size_x
			cr_cam.opts.res_y = size_y
			cr_cam.opts.blender_coord = 1
			if bl_cam.dof.use_dof:
				cr_cam.opts.fstops = bl_cam.dof.aperture_fstop
				if bl_cam.dof.focus_object:
					focus_loc = bl_cam.dof.focus_object.location
					cam_loc = bl_cam_eval.location
					# I'm sure Blender has a function for this somewhere, I couldn't find it
					dx = focus_loc.x - cam_loc.x
					dy = focus_loc.y - cam_loc.y
					dz = focus_loc.z - cam_loc.z
					distance = math.sqrt(pow(dx, 2) + pow(dy, 2) + pow(dz, 2))
					cr_cam.opts.focus_distance = distance
				else:
					cr_cam.opts.focus_distance = bl_cam.dof.focus_distance

		# Convert materials
		cr_materials = {}
		for bl_mat in bpy.data.materials:
			print("Converting {}".format(bl_mat.name))
			cr_materials[bl_mat.name] = convert_node_tree(depsgraph, bl_mat, bl_mat.node_tree)
		
		# Sync meshes
		for idx, ob_main in enumerate(objects):
			if ob_main.type != 'MESH':
				continue
			if ob_main.name in self.cr_scene.meshes:
				print("Mesh '{}' already synced, skipping".format(ob_main.name))
				break
			print("Syncing mesh {}".format(ob_main.name))
			# dump(ob_main)
			cr_mesh = self.cr_scene.mesh_new(ob_main.name)
			instances = []
			new_inst = cr_mesh.instance_new()
			new_inst.set_transform(to_cr_matrix(ob_main.matrix_world))
			cr_mat_set = self.cr_scene.material_set_new()
			new_inst.bind_materials(cr_mat_set)
			instances.append(new_inst)
			if ob_main.is_instancer and ob_main.show_instancer_for_render:
				for dup in depsgraph.object_instances:
					if dup.parent and dup.parent.original == ob_main:
						new_inst = cr_mesh.instance_new()
						new_inst.set_transform(dup.matrix_world.copy())
			ob_for_convert = ob_main.evaluated_get(depsgraph)
			try:
				me = ob_for_convert.to_mesh()
			except RuntimeError:
				me = None
			if me is None:
				print("Whoops, mesh {} couldn't be converted".format(ob_main.name))
				continue

			if len(me.materials) < 1:
				cr_mat_set.add(None)
			for bl_mat in me.materials:
				if not bl_mat:
					print("WTF, array contains NoneType?")
					cr_mat_set.add(None)
				elif bl_mat.use_nodes:
					print("Fetching material {}".format(bl_mat.name))
					if bl_mat.name not in cr_materials:
						print("Weird, {} not found in cr_materials".format(bl_mat.name))
						cr_mat_set.add(None)
					else:
						cr_mat_set.add(cr_materials[bl_mat.name])
				else:
					print("Material {} doesn't use nodes, do something about that".format(bl_mat.name))
					cr_mat_set.add(None)
			mesh_triangulate(me)
			verts = me.vertices[:]
			# me.calc_normals_split()
			faces = []
			for poly in me.polygons:
				faces.append(to_cr_face(me, poly))
			facebuf = (c_ray.cr_face * len(faces))(*faces)
			cr_mesh.bind_faces(bytearray(facebuf), len(faces))
			cr_mesh.bind_vertex_buf(cr_vertex_buf(self.cr_scene, me))
		
		# Set background shader
		bl_nodetree = bpy.data.worlds[0].node_tree
		self.cr_scene.set_background(convert_background(bl_nodetree))

	def update(self, data, depsgraph):
		start = time.time()
		if not self.cr_renderer:
			self.cr_renderer = c_ray.renderer()
			self.cr_renderer.prefs.asset_path = ""
			self.cr_renderer.prefs.blender_mode = True
			self.cr_scene = self.cr_renderer.scene_get()
		self.sync_scene(depsgraph)
		print(self.cr_scene.totals())
		# self.cr_renderer.debug_dump()
		self.cr_renderer.prefs.samples = depsgraph.scene.c_ray.samples
		self.cr_renderer.prefs.threads = depsgraph.scene.c_ray.threads
		self.cr_renderer.prefs.tile_x = depsgraph.scene.c_ray.tile_size
		self.cr_renderer.prefs.tile_y = depsgraph.scene.c_ray.tile_size
		self.cr_renderer.prefs.bounces = depsgraph.scene.c_ray.bounces
		self.cr_renderer.prefs.node_list = depsgraph.scene.c_ray.node_list
		self.cr_renderer.callbacks.on_start = (on_start, None)
		self.cr_renderer.callbacks.on_stop = (on_stop, None)
		self.cr_renderer.callbacks.on_status_update = (on_status_update, (self.cr_renderer, self.update_progress, self.update_stats, self.test_break))
		end = time.time()

	def render(self, depsgraph):
		start = time.time()
		self.cr_renderer.render()
		end = time.time()
		print("Render done (took {} seconds)".format(end - start))
		bm = self.cr_renderer.get_result()
		self.display_bitmap(bm)

	def view_draw(self, context, depsgraph):
		zoom = context.region_data.view_camera_zoom
		mtx = context.region_data.view_matrix.inverted()
		if not self.old_mtx or mtx != self.old_mtx:
			self.old_mtx = mtx
			self.tag_update()
		if not self.old_zoom or zoom != self.old_zoom:
			self.old_zoom = zoom
			self.tag_update()
		new_dims = (context.region.width, context.region.height)
		if not self.old_dims or self.old_dims != new_dims:
			cr_cam = self.cr_scene.cameras['Camera']
			cr_cam.opts.res_x = context.region.width
			cr_cam.opts.res_y = context.region.height
			self.cr_renderer.restart()
			self.old_dims = new_dims
		gpu.state.blend_set('ALPHA_PREMULT')
		self.bind_display_space_shader(depsgraph.scene)
		draw_direct(self.cr_renderer.get_result())
		self.unbind_display_space_shader()
		gpu.state.blend_set('NONE')

	def partial_update_mesh(self, update):
		mesh = update.id
		print("Mesh {} was updated".format(mesh.name))
		if update.is_updated_transform:
			# FIXME: How do I get the actual instance index from Blender?
			# Just grabbing the first one for now.
			inst = self.cr_scene.meshes[mesh.name].instances[-1]
			inst.set_transform(to_cr_matrix(mesh.matrix_world))
	def partial_update(self, updates):
		for update in updates:
			# Kind of annoying that seemingly every update has 'type', except if it's an
			# update in the Scene datablock. I'm sure there is a good reason for this though.
			if not hasattr(update.id, 'type'):
				print("Scene update")
			else:
				# print("Update, ID: '{}'".format(update.id.type))
				match update.id.type:
					case 'MESH':
						self.partial_update_mesh(update)
		# Maybe return if we actually need to restart?

	def view_update(self, context, depsgraph):
		if not self.cr_renderer:
			self.cr_renderer = c_ray.renderer()
			self.cr_renderer.prefs.asset_path = ""
			self.cr_renderer.prefs.blender_mode = True
			self.cr_scene = self.cr_renderer.scene_get()
			self.sync_scene(depsgraph)

		self.partial_update(depsgraph.updates)
		self.cr_renderer.prefs.samples = depsgraph.scene.c_ray.samples
		self.cr_renderer.prefs.threads = depsgraph.scene.c_ray.threads
		self.cr_renderer.prefs.tile_x = depsgraph.scene.c_ray.tile_size
		self.cr_renderer.prefs.tile_y = depsgraph.scene.c_ray.tile_size
		self.cr_renderer.prefs.bounces = depsgraph.scene.c_ray.bounces
		self.cr_renderer.prefs.node_list = depsgraph.scene.c_ray.node_list
		self.cr_renderer.prefs.is_iterative = 1
		cr_cam = self.cr_scene.cameras['Camera']
		mtx = context.region_data.view_matrix.inverted()
		euler = mtx.to_euler('XYZ')
		loc = mtx.to_translation()
		new_fov = math.degrees(context.space_data.camera.data.angle)
		# I haven't the faintest clue why an offset of 32 degrees makes this match perfectly.
		# If you know, please do let me know.
		if context.region_data.view_perspective == 'PERSP':
			new_fov += 32
		else:
			if self.old_zoom:
				new_fov -= (self.old_zoom - 32)
		cr_cam.opts.fov = new_fov
		cr_cam.opts.pose_x = loc[0]
		cr_cam.opts.pose_y = loc[1]
		cr_cam.opts.pose_z = loc[2]
		cr_cam.opts.pose_roll = euler[0]
		cr_cam.opts.pose_pitch = euler[1]
		cr_cam.opts.pose_yaw = euler[2]

		cr_cam.opts.res_x = context.region.width
		cr_cam.opts.res_y = context.region.height
		cr_cam.opts.blender_coord = 1

		if self.cr_interactive_running == True:
			self.cr_renderer.restart()
		else:
			print("Kicking off background renderer")
			self.cr_renderer.callbacks.on_interactive_pass_finished = (status_update_interactive, (self.tag_redraw, self.update_stats, self))
			self.cr_renderer.start_interactive()
			self.cr_interactive_running = True

	def display_bitmap(self, bm):
		# Get float array from libc-ray
		print("Grabbing float array from lib")
		start_first = time.time()
		float_count = bm.width * bm.height * bm.stride
		buffer_from_memory = ct.pythonapi.PyMemoryView_FromMemory
		buffer_from_memory.restype = ct.py_object
		buffer = buffer_from_memory(bm.data.float_ptr, 4 * float_count)
		end = time.time()
		print("Done, took {}s".format(end - start_first))

		print("Converting")
		start = time.time()
		floats = np.frombuffer(buffer, np.float32)
		end = time.time()
		print("DONE, took {}".format(end - start))
		result = self.begin_result(0, 0, bm.width, bm.height)
		r_pass = result.layers[0].passes["Combined"]
		print("Copying to RenderPass")
		start = time.time()
		r_pass.rect.foreach_set(floats)
		end = time.time()
		print("Done, took {}".format(end - start))
		print("Displaying bitmap took a total of {}s".format(end - start_first))

		self.end_result(result)

def register():
	from . import properties
	from . import ui
	import faulthandler
	faulthandler.enable()
	print("Register libc-ray version {} ({})".format(c_ray.version.semantic, c_ray.version.githash))
	properties.register()
	ui.register()
	bpy.utils.register_class(CrayRender)
	bpy.utils.register_class(CrayRenderSettings)

    
def unregister():
	from . import properties
	from . import ui
	print("Unregister libc-ray version {} ({})".format(c_ray.version.semantic, c_ray.version.githash))

	properties.unregister()
	ui.unregister()

	bpy.utils.unregister_class(CrayRenderSettings)
	bpy.utils.unregister_class(CrayRender)

    
if __name__ == "__main__":
	register()
